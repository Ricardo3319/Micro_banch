#!/usr/bin/env python3
import csv
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path('/users/Mingyang/Micro_banch')
BUILD = ROOT / 'build-wp2-release'
OUT = Path(sys.argv[1]).resolve()
SEEDS = [71, 83, 97, 109, 127]
ORDER_SEED = 2026072602
WORKERS = 8
ARRIVAL_SCALE = 10.0
CHECK_PERIOD_US = 100
VARIANTS = {
    'FIFO': ['--policy', 'L0_RandomCore'],
    'M0': ['--policy', 'M0_AltoThreshold'],
    'L1Only': ['--policy', 'L1_WorkStealingPolling'],
    'CentralOnly': ['--policy', 'M1_RescueSched', '--disable-rescue-distributed-l1'],
    'FullHybrid': ['--policy', 'M1_RescueSched'],
}


def read_one(path):
    with path.open(newline='') as handle:
        return next(csv.DictReader(handle))


def read_env(path):
    result = {}
    for line in path.read_text().splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            result[key] = value
    return result


def log_command(kind, seed, variant, command):
    with (OUT / 'commands.jsonl').open('a') as handle:
        handle.write(json.dumps({'kind': kind, 'setup': 'RPC-W3-rho085-w8-scale10',
            'seed': seed, 'variant': variant, 'cwd': str(ROOT), 'command': command},
            sort_keys=True) + '\n')


def generate_trace(seed):
    trace = OUT / 'rpc-traces' / f'seed-{seed}.csv'
    if trace.exists():
        return trace
    trace.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(BUILD / 'rescuesched_trace_generator'), '--out', str(trace),
           '--workload', 'W3', '--rho', '0.85', '--seed', str(seed),
           '--workers', str(WORKERS), '--warmup', '500', '--requests', '5000',
           '--flow-count', '256', '--flow-zipf-alpha', '0',
           '--flow-hash-seed', '0x52535331']
    log_command('rpc_trace_generate', seed, '', cmd)
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    (OUT / 'rpc-trace-logs').mkdir(parents=True, exist_ok=True)
    (OUT / 'rpc-trace-logs' / f'seed-{seed}.log').write_text(
        '$ ' + ' '.join(cmd) + f'\nreturncode={proc.returncode}\n--- stdout ---\n'
        + proc.stdout + '\n--- stderr ---\n' + proc.stderr)
    if proc.returncode != 0:
        raise RuntimeError(f'RPC trace generation failed for seed {seed}')
    return trace


def order(seed):
    items = []
    for variant in VARIANTS:
        digest = hashlib.sha256(f'{ORDER_SEED}:{seed}:{variant}'.encode()).hexdigest()
        items.append((digest, variant))
    return [variant for _, variant in sorted(items)]


def validate(seed, variant, run_dir):
    server = run_dir / 'server'
    client = run_dir / 'client'
    ss = read_env(server / 'RPC_SERVER_STATUS.txt')
    cs = read_env(client / 'RPC_CLIENT_STATUS.txt')
    summary = read_one(server / 'summary.csv')
    client_summary = read_one(client / 'client_summary.csv')
    manifest = read_env(server / 'manifest.env')
    errors = []
    warnings = []
    required_server_zero = ['invalid_packets', 'duplicate_packets', 'unknown_requests',
                            'flow_mismatch_requests', 'response_enqueue_failures',
                            'response_send_failures', 'receiver_internal_failures']
    if ss.get('status') != 'PASS' or ss.get('classification') != 'VALID_COMPLETE':
        errors.append('server status/classification failed')
    if cs.get('status') != 'PASS': errors.append('client status failed')
    for key in required_server_zero:
        if ss.get(key, '0') != '0': errors.append(f'server {key}={ss.get(key)}')
    for key in ['timeouts', 'send_failures', 'invalid_responses', 'duplicate_responses']:
        if cs.get(key) != '0': errors.append(f'client {key}={cs.get(key)}')
    for key in ['receiver_affinity_pass', 'response_sender_affinity_pass',
                'scheduler_affinity_pass', 'server_main_affinity_pass',
                'runtime_invariants_pass']:
        if ss.get(key) != '1': errors.append(f'{key}={ss.get(key)}')
    if client_summary.get('sender_affinity_pass') != '1': errors.append('sender affinity failed')
    if client_summary.get('receiver_affinity_pass') != '1': errors.append('client receiver affinity failed')
    for key, expected in [('expected_requests', '5500'), ('accepted_requests', '5500'),
                          ('responses_enqueued', '5500'), ('responses_sent', '5500')]:
        if ss.get(key) != expected: errors.append(f'{key}={ss.get(key)} expected={expected}')
    for key, expected in [('total_requests', '5500'), ('measurement_requests', '5000'),
                          ('completed_requests', '5500'), ('cancelled_requests', '0'),
                          ('duplicate_execution_count', '0'),
                          ('duplicate_completion_count', '0'),
                          ('lost_descriptor_count', '0'),
                          ('nonzero_reservation_count', '0'),
                          ('affinity_failure_count', '0'), ('invariants_pass', '1')]:
        if summary.get(key) != expected: errors.append(f'summary {key}={summary.get(key)}')
    for key, expected in [('total_requests', '5500'), ('measurement_requests', '5000'),
                          ('responses', '5500'), ('measurement_responses', '5000'),
                          ('status', 'PASS')]:
        if client_summary.get(key) != expected:
            errors.append(f'client summary {key}={client_summary.get(key)} expected={expected}')
    if float(client_summary.get('send_lag_p99_us', 'inf')) > 4.0:
        warnings.append(f"send_lag_p99_us={client_summary.get('send_lag_p99_us')} > 4")
    scheduled = int(summary['scheduler_epochs_scheduled'])
    missed = int(summary['scheduler_epochs_missed'])
    if scheduled and missed / scheduled > 0.05:
        warnings.append(f'scheduler missed fraction {missed/scheduled:.6f} > 0.05')
    if manifest.get('moves_per_check') != '4' or float(manifest.get('epsilon_us', 'nan')) != 6.0 \
            or float(manifest.get('rescue_lookahead_us', 'nan')) != 20.0 \
            or manifest.get('rescue_idle_pulls_per_check') != '0':
        errors.append('fixed M1 parameter identity mismatch')
    expected_l1 = '0' if variant == 'CentralOnly' else '1'
    if variant in {'CentralOnly', 'FullHybrid'} and manifest.get(
            'rescue_distributed_l1_enabled') != expected_l1:
        errors.append('central/full mechanism identity mismatch')
    if errors:
        raise RuntimeError(f"invalid RPC run seed={seed} {variant}: {'; '.join(errors)}")
    return warnings


def run_one(run_no, seed, variant, port):
    trace = generate_trace(seed)
    run_dir = OUT / 'rpc' / 'RPC-W3-rho085-w8-scale10' / f'seed-{seed}' / variant
    server_dir = run_dir / 'server'
    client_dir = run_dir / 'client'
    run_dir.mkdir(parents=True, exist_ok=True)
    common_runtime = [
        *VARIANTS[variant], '--workers', str(WORKERS),
        '--cpus', '0,1,2,3,4,5,6,7', '--warmup-requests', '500',
        '--check-period-us', str(CHECK_PERIOD_US), '--scan-depth', '64', '--k-candidates', '16',
        '--h-targets', '4', '--moves-per-check', '4', '--epsilon-us', '6',
        '--rescue-lookahead-us', '20', '--rescue-gain-weight', '0.05',
        '--rescue-target-backlog-penalty', '1', '--rescue-idle-pulls-per-check', '0',
        '--handoff-estimate-us', '0.5', '--host-overhead-us', '2.1',
        '--ewma-alpha', '0.05', '--alto-threshold-us', '40', '--alto-min-gain-us', '0',
        '--decision-sample-cap', '100000', '--decision-bucket-us', '1000',
        '--workload-label', 'W3', '--rho-label', '0.85', '--seed-label', str(seed),
        '--repetition', '1',
    ]
    server_cmd = [
        str(BUILD / 'rescuesched_rpc_server'), '--trace', str(trace),
        '--out-dir', str(server_dir), '--bind', '127.0.0.1', '--port', str(port),
        '--idle-timeout-seconds', '2', '--receiver-cpus', '8,9',
        '--response-sender-cpus', '10,11', '--scheduler-cpu', '12',
        '--server-main-cpu', '13', '--response-queue-capacity', '65536',
        *common_runtime,
    ]
    client_cmd = [
        str(BUILD / 'rescuesched_rpc_client'), '--trace', str(trace),
        '--server', '127.0.0.1', '--out-dir', str(client_dir), '--port', str(port),
        '--workers', str(WORKERS), '--flow-sockets', '256',
        '--arrival-scale', str(ARRIVAL_SCALE), '--client-index', '0', '--client-count', '1',
        '--source-port-base', '30000', '--bind', '127.0.0.1',
        '--sender-cpu', '14', '--receiver-cpu', '15',
        '--response-timeout-seconds', '5', '--warmup-requests', '500',
        '--workload-label', 'W3', '--rho-label', '0.85', '--seed-label', str(seed),
        '--repetition', '1',
    ]
    log_command('rpc_server', seed, variant, server_cmd)
    log_command('rpc_client', seed, variant, client_cmd)
    server_log = (run_dir / 'server-console.log').open('w')
    started = time.monotonic()
    server = subprocess.Popen(server_cmd, cwd=ROOT, stdout=server_log,
                              stderr=subprocess.STDOUT, text=True)
    try:
        ready = False
        for _ in range(200):
            server_log.flush()
            text = (run_dir / 'server-console.log').read_text(errors='replace')
            if 'RPC_SERVER_READY' in text:
                ready = True
                break
            if server.poll() is not None:
                break
            time.sleep(0.05)
        if not ready:
            raise RuntimeError(f'RPC server did not become ready seed={seed} {variant}')
        client_timed_out = False
        try:
            client = subprocess.run(client_cmd, cwd=ROOT, text=True,
                                    capture_output=True, timeout=15)
        except subprocess.TimeoutExpired as error:
            client_timed_out = True
            stdout = error.stdout or ''
            stderr = error.stderr or ''
            if isinstance(stdout, bytes): stdout = stdout.decode(errors='replace')
            if isinstance(stderr, bytes): stderr = stderr.decode(errors='replace')
            client = subprocess.CompletedProcess(client_cmd, 124, stdout, stderr)
        (run_dir / 'client-console.log').write_text(
            '$ ' + ' '.join(client_cmd)
            + f'\nreturncode={client.returncode}\ntimeout_s=15'
            + f'\ntimed_out={int(client_timed_out)}\n--- stdout ---\n'
            + client.stdout + '\n--- stderr ---\n' + client.stderr)
        if client.returncode != 0:
            raise RuntimeError(f'RPC client failed seed={seed} {variant} '
                               f'rc={client.returncode} timed_out={int(client_timed_out)}')
        try:
            server_rc = server.wait(timeout=15)
        except subprocess.TimeoutExpired:
            server.terminate()
            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()
            raise RuntimeError(f'RPC server timeout seed={seed} {variant}')
        if server_rc != 0:
            raise RuntimeError(f'RPC server failed rc={server_rc} seed={seed} {variant}')
    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()
        server_log.close()
    elapsed = time.monotonic() - started
    warnings = validate(seed, variant, run_dir)
    validity = 'VALID_COMPLETE' if not warnings else 'VALID_WITH_TIMING_WARNING'
    (run_dir / 'EXPERIMENT_VALIDITY.txt').write_text(
        'correctness_status=PASS\n'
        + f'timing_status={validity}\n'
        + f'warning_count={len(warnings)}\n'
        + ''.join(f'warning={warning}\n' for warning in warnings))
    if warnings:
        with (OUT / 'rpc-quality-warnings.log').open('a') as handle:
            handle.write(f'seed={seed} variant={variant} ' + '; '.join(warnings) + '\n')
    with (OUT / 'rpc-progress.log').open('a') as handle:
        handle.write(f'{run_no}/25 seed={seed} variant={variant} port={port} '
                     f'elapsed_s={elapsed:.6f} status={validity} '
                     f'warning_count={len(warnings)}\n')


def main():
    for exe in ['rescuesched_trace_generator', 'rescuesched_rpc_server',
                'rescuesched_rpc_client']:
        if not (BUILD / exe).is_file():
            raise RuntimeError(f'missing executable: {BUILD/exe}')
    plan = {'attempt': 2,
            'restart_reason': 'check_period_5us_was_physically_unschedulable_in_network_time_scale_1',
            'order_seed': ORDER_SEED, 'seeds': SEEDS, 'variants': list(VARIANTS),
            'workers': WORKERS, 'arrival_scale': ARRIVAL_SCALE,
            'check_period_us': CHECK_PERIOD_US,
            'fixed_m1': {'moves': 4, 'epsilon_us': 6, 'lookahead_us': 20, 'idle_pulls': 0},
            'timing_warning_thresholds_non_exclusionary': {
                'client_send_lag_p99_us': 4.0, 'scheduler_missed_fraction': 0.05},
            'cpu_layout': {'server_workers': list(range(8)), 'receivers': [8, 9],
                           'response_senders': [10, 11], 'scheduler': 12,
                           'server_main': 13, 'client_sender': 14,
                           'client_receiver': 15},
            'scope': 'single-host-loopback-real-UDP-RPC-low-load-runtime-validation'}
    (OUT / 'rpc_run_plan.json').write_text(json.dumps(plan, indent=2, sort_keys=True))
    run_no = 0
    for seed in SEEDS:
        for variant in order(seed):
            run_no += 1
            run_one(run_no, seed, variant, 24000 + run_no)
    (OUT / 'rpc-complete.txt').write_text(
        f'status=PASS\nruns={run_no}\narrival_scale={ARRIVAL_SCALE}\n'
        'scope=single_host_loopback_real_udp_rpc_low_load_runtime_validation\n')

if __name__ == '__main__':
    main()
