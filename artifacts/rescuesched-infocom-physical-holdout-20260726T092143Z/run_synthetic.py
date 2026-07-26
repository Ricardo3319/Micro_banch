#!/usr/bin/env python3
import csv
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path('/users/Mingyang/Micro_banch')
BUILD = ROOT / 'build-wp2-release'
OUT = Path(sys.argv[1]).resolve()
EXISTING = ROOT / 'artifacts/comprehensive-test-20260721T094700Z/runtime/traces'
SEEDS = [71, 83, 97, 109, 127]
ORDER_SEED = 20260726
COMMON_VARIANTS = {
    'FIFO': ['--policy', 'L0_RandomCore'],
    'M0': ['--policy', 'M0_AltoThreshold'],
    'L1Only': ['--policy', 'L1_WorkStealingPolling'],
    'FullHybrid': ['--policy', 'M1_RescueSched'],
}
PRIMARY_VARIANTS = {
    **COMMON_VARIANTS,
    'CentralOnly': ['--policy', 'M1_RescueSched', '--disable-rescue-distributed-l1'],
}
SETUPS = {
    'W3-rho070-w16': {'workload': 'W3', 'rho': '0.70', 'workers': 16,
                      'existing': 'W3-rho070', 'variants': PRIMARY_VARIANTS,
                      'role': 'primary_holdout_and_ablation'},
    'W3-rho085-w16': {'workload': 'W3', 'rho': '0.85', 'workers': 16,
                      'existing': 'W3-rho085', 'variants': PRIMARY_VARIANTS,
                      'role': 'primary_holdout_and_ablation'},
    'W3-rho090-w16': {'workload': 'W3', 'rho': '0.90', 'workers': 16,
                      'existing': 'W3-rho090', 'variants': PRIMARY_VARIANTS,
                      'role': 'primary_holdout_and_ablation'},
    'W2-rho085-w16': {'workload': 'W2', 'rho': '0.85', 'workers': 16,
                      'existing': 'W2-rho085', 'variants': PRIMARY_VARIANTS,
                      'role': 'primary_holdout_and_ablation'},
    'W2-rho070-w16': {'workload': 'W2', 'rho': '0.70', 'workers': 16,
                      'variants': COMMON_VARIANTS,
                      'role': 'supplemental_cross_load'},
    'W3-rho085-w8': {'workload': 'W3', 'rho': '0.85', 'workers': 8,
                     'variants': COMMON_VARIANTS,
                     'role': 'supplemental_worker_count'},
    'W3-rho085-w12': {'workload': 'W3', 'rho': '0.85', 'workers': 12,
                      'variants': COMMON_VARIANTS,
                      'role': 'supplemental_worker_count'},
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


def log_command(kind, setup, seed, variant, command):
    row = {'kind': kind, 'setup': setup, 'seed': seed, 'variant': variant,
           'cwd': str(ROOT), 'command': command}
    with (OUT / 'commands.jsonl').open('a') as handle:
        handle.write(json.dumps(row, sort_keys=True) + '\n')


def run_logged(command, log_path, timeout_s=30):
    start = time.monotonic()
    timed_out = False
    try:
        proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                              timeout=timeout_s)
    except subprocess.TimeoutExpired as error:
        timed_out = True
        stdout = error.stdout or ''
        stderr = error.stderr or ''
        if isinstance(stdout, bytes): stdout = stdout.decode(errors='replace')
        if isinstance(stderr, bytes): stderr = stderr.decode(errors='replace')
        proc = subprocess.CompletedProcess(command, 124, stdout, stderr)
    elapsed = time.monotonic() - start
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        '$ ' + ' '.join(command)
        + f'\nreturncode={proc.returncode}\nelapsed_s={elapsed:.6f}'
        + f'\ntimeout_s={timeout_s}\ntimed_out={int(timed_out)}\n'
        + '--- stdout ---\n' + proc.stdout + '\n--- stderr ---\n' + proc.stderr)
    return proc, elapsed


def trace_path(setup, cfg, seed):
    if 'existing' in cfg:
        return EXISTING / cfg['existing'] / f'seed-{seed}.csv'
    return OUT / 'traces' / setup / f'seed-{seed}.csv'


def ensure_trace(setup, cfg, seed):
    path = trace_path(setup, cfg, seed)
    if path.exists():
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(BUILD / 'rescuesched_trace_generator'), '--out', str(path),
        '--workload', cfg['workload'], '--rho', cfg['rho'], '--seed', str(seed),
        '--workers', str(cfg['workers']), '--warmup', '500', '--requests', '5000',
        '--flow-count', '4096', '--flow-zipf-alpha', '0',
        '--flow-hash-seed', '0x52535331',
    ]
    log_command('trace_generate', setup, seed, '', command)
    proc, _ = run_logged(command, OUT / 'trace-generation-logs' / setup / f'seed-{seed}.log')
    if proc.returncode != 0:
        raise RuntimeError(f'trace generation failed: {setup} seed={seed}')
    return path


def validate_trace(path, workers):
    lines = path.read_text().splitlines()
    if len(lines) != 5501:
        raise RuntimeError(f'unexpected trace row count: {path}: {len(lines)}')
    with path.open(newline='') as handle:
        rows = csv.DictReader(handle)
        embedded = None
        count = 0
        for row in rows:
            count += 1
            if embedded is None:
                embedded = row['trace_sha256']
            if row['trace_sha256'] != embedded:
                raise RuntimeError(f'mixed embedded hashes: {path}')
            core = int(row['initial_core'])
            if core < 0 or core >= workers:
                raise RuntimeError(f'initial core {core} outside worker count {workers}: {path}')
        if count != 5500 or embedded is None or len(embedded) != 64:
            raise RuntimeError(f'invalid trace contents: {path}')


def variant_order(setup, seed, variants):
    names = list(variants)
    keyed = []
    for name in names:
        token = f'{ORDER_SEED}:{setup}:{seed}:{name}'.encode()
        keyed.append((hashlib.sha256(token).hexdigest(), name))
    return [name for _, name in sorted(keyed)]


def validate_run(run_dir, cfg, variant):
    status = read_env(run_dir / 'RUNTIME_STATUS.txt')
    summary = read_one(run_dir / 'summary.csv')
    manifest = read_env(run_dir / 'manifest.env')
    errors = []
    if status.get('status') != 'PASS': errors.append('runtime status is not PASS')
    if summary.get('classification') != 'VALID_COMPLETE': errors.append('not VALID_COMPLETE')
    if summary.get('invariants_pass') != '1': errors.append('invariants failed')
    for key, expected in [('total_requests', '5500'), ('measurement_requests', '5000'),
                          ('completed_requests', '5500'), ('cancelled_requests', '0'),
                          ('duplicate_execution_count', '0'),
                          ('duplicate_completion_count', '0'),
                          ('lost_descriptor_count', '0'),
                          ('nonzero_reservation_count', '0'),
                          ('affinity_failure_count', '0')]:
        if summary.get(key) != expected:
            errors.append(f'{key}={summary.get(key)} expected={expected}')
    if manifest.get('worker_count') != str(cfg['workers']): errors.append('worker_count mismatch')
    if manifest.get('moves_per_check') != '4': errors.append('moves_per_check mismatch')
    if float(manifest.get('epsilon_us', 'nan')) != 6.0: errors.append('epsilon mismatch')
    if float(manifest.get('rescue_lookahead_us', 'nan')) != 20.0: errors.append('lookahead mismatch')
    if manifest.get('rescue_idle_pulls_per_check') != '0': errors.append('idle pulls mismatch')
    expected_l1 = '0' if variant == 'CentralOnly' else '1'
    if variant in {'CentralOnly', 'FullHybrid'} and manifest.get(
            'rescue_distributed_l1_enabled') != expected_l1:
        errors.append('rescue distributed L1 ablation mismatch')
    scheduled = int(summary['scheduler_epochs_scheduled'])
    missed = int(summary['scheduler_epochs_missed'])
    if scheduled and missed / scheduled > 0.05:
        errors.append(f'scheduler missed fraction {missed/scheduled:.6f} > 0.05')
    if errors:
        raise RuntimeError(f"invalid run {run_dir}: {'; '.join(errors)}")


def main():
    for exe in ['rescuesched_physical_runtime', 'rescuesched_trace_generator']:
        if not (BUILD / exe).is_file():
            raise RuntimeError(f'missing executable: {BUILD/exe}')
    OUT.mkdir(parents=True, exist_ok=True)
    total = sum(len(cfg['variants']) * len(SEEDS) for cfg in SETUPS.values())
    run_no = 0
    with (OUT / 'synthetic_run_plan.json').open('w') as handle:
        json.dump({'attempt': 2, 'restart_reason': 'post-liveness-fix full clean paired restart', 'order_seed': ORDER_SEED, 'seeds': SEEDS, 'setups': {
            name: {k: (list(v) if k == 'variants' else v) for k, v in cfg.items()}
            for name, cfg in SETUPS.items()}}, handle, indent=2, sort_keys=True)
    for setup, cfg in SETUPS.items():
        for seed in SEEDS:
            trace = ensure_trace(setup, cfg, seed)
            validate_trace(trace, cfg['workers'])
            for variant in variant_order(setup, seed, cfg['variants']):
                run_no += 1
                run_dir = OUT / 'synthetic' / setup / f'seed-{seed}' / variant
                command = [
                    str(BUILD / 'rescuesched_physical_runtime'),
                    '--trace', str(trace), '--out-dir', str(run_dir),
                    *cfg['variants'][variant], '--workers', str(cfg['workers']),
                    '--cpus', ','.join(str(i) for i in range(cfg['workers'])),
                    '--warmup-requests', '500', '--time-scale', '20',
                    '--check-period-us', '5', '--scan-depth', '64',
                    '--k-candidates', '16', '--h-targets', '4',
                    '--moves-per-check', '4', '--epsilon-us', '6',
                    '--rescue-lookahead-us', '20', '--rescue-gain-weight', '0.05',
                    '--rescue-target-backlog-penalty', '1',
                    '--rescue-idle-pulls-per-check', '0',
                    '--handoff-estimate-us', '0.5', '--host-overhead-us', '2.1',
                    '--ewma-alpha', '0.05', '--alto-threshold-us', '40',
                    '--alto-min-gain-us', '0', '--decision-sample-cap', '100000',
                    '--decision-bucket-us', '1000',
                    '--workload-label', cfg['workload'], '--rho-label', cfg['rho'],
                    '--seed-label', str(seed), '--repetition', '1',
                ]
                log_command('synthetic_runtime', setup, seed, variant, command)
                started = time.time()
                proc, elapsed = run_logged(command, run_dir.parent / f'{variant}.console.log', timeout_s=10)
                with (OUT / 'synthetic-progress.log').open('a') as handle:
                    handle.write(f'{run_no}/{total} {setup} seed={seed} variant={variant} '
                                 f'rc={proc.returncode} elapsed_s={elapsed:.6f} '
                                 f'unix_start={started:.6f}\n')
                if proc.returncode != 0:
                    raise RuntimeError(f'run failed: {setup} seed={seed} {variant}')
                validate_run(run_dir, cfg, variant)
    (OUT / 'synthetic-complete.txt').write_text(
        f'status=PASS\nruns={run_no}\nexpected_runs={total}\n')

if __name__ == '__main__':
    main()
