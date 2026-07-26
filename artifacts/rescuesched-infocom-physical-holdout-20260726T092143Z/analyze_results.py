#!/usr/bin/env python3
import csv
import hashlib
import itertools
import json
import math
import random
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path

OUT = Path(sys.argv[1]).resolve()
BOOTSTRAP_DRAWS = 10000
BOOTSTRAP_SEED = 2026072603
HOST_OVERHEAD_US = 2.1


def read_rows(path):
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def read_one(path):
    rows = read_rows(path)
    if len(rows) != 1:
        raise RuntimeError(f'expected one row in {path}, got {len(rows)}')
    return rows[0]


def read_env(path):
    result = {}
    for line in path.read_text().splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            result[key] = value
    return result


def f(row, key, default=0.0):
    value = row.get(key, '')
    return default if value in ('', None) else float(value)


def i(row, key, default=0):
    value = row.get(key, '')
    return default if value in ('', None) else int(value)


def quantile(values, q):
    values = sorted(float(value) for value in values)
    if not values:
        return math.nan
    if len(values) == 1:
        return values[0]
    position = (len(values) - 1) * q
    lo = math.floor(position)
    hi = math.ceil(position)
    if lo == hi:
        return values[lo]
    fraction = position - lo
    return values[lo] * (1.0 - fraction) + values[hi] * fraction


def safe_div(num, den):
    return math.nan if den == 0 else num / den


def finite_text(value):
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return ''
    return value


def write_csv(path, rows, fieldnames=None):
    rows = list(rows)
    if fieldnames is None:
        if not rows:
            raise RuntimeError(f'cannot infer fields for empty CSV {path}')
        fieldnames = list(rows[0])
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction='ignore')
        writer.writeheader()
        for row in rows:
            writer.writerow({key: finite_text(row.get(key, '')) for key in fieldnames})


def status_validity(scope, summary, manifest, status, client_status=None, client_summary=None):
    errors = []
    warnings = []
    if summary.get('classification') != 'VALID_COMPLETE': errors.append('classification')
    if summary.get('invariants_pass') != '1': errors.append('invariants_pass')
    if summary.get('completed_requests') != summary.get('total_requests'):
        errors.append('request_completeness')
    for key in ['duplicate_execution_count', 'duplicate_completion_count',
                'lost_descriptor_count', 'nonzero_reservation_count',
                'affinity_failure_count']:
        if i(summary, key) != 0: errors.append(key)
    if status.get('status') != 'PASS': errors.append('runtime_or_server_status')
    if manifest.get('moves_per_check') != '4': errors.append('moves')
    if f(manifest, 'epsilon_us', math.nan) != 6.0: errors.append('epsilon')
    if f(manifest, 'rescue_lookahead_us', math.nan) != 20.0: errors.append('lookahead')
    if manifest.get('rescue_idle_pulls_per_check') != '0': errors.append('idle_pulls')
    scheduled = i(summary, 'scheduler_epochs_scheduled')
    missed = i(summary, 'scheduler_epochs_missed')
    if scheduled and missed / scheduled > 0.05: errors.append('scheduler_missed_fraction')
    if scope == 'loopback_udp_rpc':
        if manifest.get('check_period_us') != '100': errors.append('rpc_check_period_us')
        for key in ['invalid_packets', 'duplicate_packets', 'unknown_requests',
                    'flow_mismatch_requests', 'response_enqueue_failures',
                    'response_send_failures', 'receiver_internal_failures']:
            if i(status, key) != 0: errors.append('server_' + key)
        for key in ['timeouts', 'send_failures', 'invalid_responses', 'duplicate_responses']:
            if i(client_status, key) != 0: errors.append('client_' + key)
        if client_status.get('status') != 'PASS': errors.append('client_status')
        if client_summary.get('status') != 'PASS': errors.append('client_summary_status')
        if f(client_summary, 'send_lag_p99_us', math.inf) > 4.0:
            warnings.append('client_send_lag_p99_gt_4us')
    return errors, warnings


def run_descriptors():
    for setup_dir in sorted((OUT / 'synthetic').glob('*')):
        for seed_dir in sorted(setup_dir.glob('seed-*')):
            for variant_dir in sorted(path for path in seed_dir.iterdir() if path.is_dir()):
                yield {'scope': 'local_synthetic_runtime', 'setup': setup_dir.name,
                       'seed': int(seed_dir.name.split('-', 1)[1]),
                       'variant': variant_dir.name, 'server_dir': variant_dir,
                       'client_dir': None}
    for setup_dir in sorted((OUT / 'rpc').glob('*')):
        for seed_dir in sorted(setup_dir.glob('seed-*')):
            for variant_dir in sorted(path for path in seed_dir.iterdir() if path.is_dir()):
                yield {'scope': 'loopback_udp_rpc', 'setup': setup_dir.name,
                       'seed': int(seed_dir.name.split('-', 1)[1]),
                       'variant': variant_dir.name, 'server_dir': variant_dir / 'server',
                       'client_dir': variant_dir / 'client'}


def analyze_run(desc):
    root = desc['server_dir']
    summary = read_one(root / 'summary.csv')
    manifest = read_env(root / 'manifest.env')
    status_file = root / ('RPC_SERVER_STATUS.txt' if desc['scope'] == 'loopback_udp_rpc'
                          else 'RUNTIME_STATUS.txt')
    status = read_env(status_file)
    requests = read_rows(root / 'requests.csv')
    migrations = read_rows(root / 'migrations.csv')
    decisions = read_rows(root / 'decisions.csv')
    aggregates = read_rows(root / 'decision_aggregates.csv')
    measurement = [row for row in requests if row['measurement_eligible'] == '1'
                   and row['state'] == 'DONE']
    request_by_id = {int(row['request_id']): row for row in requests}
    migration_counts = Counter(int(row['request_id']) for row in migrations
                               if row['outcome'] == 'committed_append_tail')
    central_rows = [row for row in migrations
                    if row['migration_reason'] == 'commit_local_doom_remote_feasible'
                    and row['outcome'] == 'committed_append_tail']
    central_ids = {int(row['request_id']) for row in central_rows}
    central_measurement_ids = {int(row['request_id']) for row in measurement} & central_ids
    central_success = sum(request_by_id[rid]['deadline_violation'] == '0'
                          for rid in central_measurement_ids)
    total_decision_ns = sum(i(row, 'total_decision_duration_ns') for row in aggregates)
    max_decision_ns = max([i(row, 'max_decision_duration_ns') for row in aggregates] or [0])
    l1_ns = i(summary, 'l1_poll_total_cost_ns')
    central_control_ns = max(0, total_decision_ns - l1_ns)
    all_decisions_captured = i(summary, 'decision_records_dropped') == 0 \
        and len(decisions) == i(summary, 'decision_records_total')
    scanned = sum(i(row, 'scanned_entries') for row in decisions) if all_decisions_captured else math.nan
    targets = sum(i(row, 'evaluated_targets') for row in decisions) if all_decisions_captured else math.nan
    cycles = sum(i(row, 'decision_cycles') for row in decisions) if all_decisions_captured else math.nan
    moved_work = 0.0
    for row in migrations:
        if row['outcome'] != 'committed_append_tail':
            continue
        req = request_by_id[int(row['request_id'])]
        moved_work += f(req, 'estimated_service_us') + HOST_OVERHEAD_US
    handoff_ns = [i(row, 'handoff_duration_ns') for row in migrations
                  if row['outcome'] == 'committed_append_tail']
    client_summary = {}
    client_status = {}
    client_requests = []
    client_rtts = []
    client_slo_goodput = math.nan
    if desc['client_dir']:
        client_summary = read_one(desc['client_dir'] / 'client_summary.csv')
        client_status = read_env(desc['client_dir'] / 'RPC_CLIENT_STATUS.txt')
        client_requests = read_rows(desc['client_dir'] / 'client_requests.csv')
        eligible_client = [row for row in client_requests
                           if row['measurement_eligible'] == '1'
                           and row['response_status'] == 'received']
        client_rtts = [f(row, 'client_rtt_us') for row in eligible_client]
        if eligible_client:
            duration_us = max(f(row, 'client_receive_us') for row in eligible_client) \
                - min(f(row, 'actual_send_us') for row in eligible_client)
            successes = sum(row['server_deadline_violation'] == '0' for row in eligible_client)
            client_slo_goodput = safe_div(successes * 1e6, duration_us)
    errors, warnings = status_validity(desc['scope'], summary, manifest, status,
                                       client_status, client_summary)
    total_measurement = i(summary, 'measurement_requests')
    raw = {
        **{key: desc[key] for key in ['scope', 'setup', 'seed', 'variant']},
        'workload': manifest.get('workload', ''), 'rho': manifest.get('rho', ''),
        'workers': i(manifest, 'worker_count'), 'trace_embedded_sha256': manifest.get('trace_embedded_sha256', ''),
        'trace_input_file_sha256': manifest.get('trace_input_file_sha256', ''),
        'commit': read_env(OUT / 'run_metadata.env').get('commit', ''), 'dirty': 1,
        'classification': summary.get('classification', ''),
        'valid': 0 if errors else 1, 'validity_errors': ';'.join(errors),
        'validity_warnings': ';'.join(warnings),
        'measurement_requests': total_measurement,
        'deadline_violations': i(summary, 'deadline_violations'),
        'deadline_violation_rate': f(summary, 'deadline_violation_rate'),
        'slo_goodput_rps': f(summary, 'goodput_rps'),
        'server_p50_us': f(summary, 'P50_server_completion_us'),
        'server_p99_us': f(summary, 'P99_server_completion_us'),
        'server_p999_us': f(summary, 'P999_server_completion_us'),
        'client_p50_rtt_us': f(client_summary, 'P50_client_rtt_us', math.nan),
        'client_p99_rtt_us': f(client_summary, 'P99_client_rtt_us', math.nan),
        'client_p999_rtt_us': f(client_summary, 'P999_client_rtt_us', math.nan),
        'client_slo_goodput_rps': client_slo_goodput,
        'client_send_lag_p99_us': f(client_summary, 'send_lag_p99_us', math.nan),
        'migration_count': i(summary, 'migration_count'),
        'migrated_requests': i(summary, 'migrated_requests'),
        'repeat_migrations': i(summary, 'migration_count') - i(summary, 'migrated_requests'),
        'max_migrations_per_request': max(migration_counts.values() or [0]),
        'central_commits': len(central_rows), 'central_unique_requests': len(central_measurement_ids),
        'central_rescue_successes': central_success,
        'central_rescue_success_rate': safe_div(central_success, len(central_measurement_ids)),
        'moved_estimated_work_us': moved_work,
        'handoff_total_ns': sum(handoff_ns), 'handoff_p99_ns': quantile(handoff_ns, .99),
        'decision_records_total': i(summary, 'decision_records_total'),
        'decision_records_sampled': i(summary, 'decision_records_sampled'),
        'decision_records_dropped': i(summary, 'decision_records_dropped'),
        'all_decisions_captured': 1 if all_decisions_captured else 0,
        'control_path_total_duration_ns': total_decision_ns,
        'central_scheduler_duration_ns': central_control_ns,
        'distributed_l1_duration_ns': l1_ns,
        'control_path_ns_per_request': safe_div(total_decision_ns, total_measurement),
        'central_scheduler_ns_per_request': safe_div(central_control_ns, total_measurement),
        'distributed_l1_ns_per_request': safe_div(l1_ns, total_measurement),
        'scanned_entries_total': scanned, 'scanned_entries_per_request': safe_div(scanned, total_measurement),
        'evaluated_targets_total': targets, 'decision_cycles_total': cycles,
        'scheduler_epochs_scheduled': i(summary, 'scheduler_epochs_scheduled'),
        'scheduler_epochs_executed': i(summary, 'scheduler_epochs_executed'),
        'scheduler_epochs_missed': i(summary, 'scheduler_epochs_missed'),
        'scheduler_missed_fraction': safe_div(i(summary, 'scheduler_epochs_missed'),
                                              i(summary, 'scheduler_epochs_scheduled')),
        'scheduler_max_epoch_lag_us': f(summary, 'scheduler_max_epoch_lag_us'),
        'l1_poll_attempts': i(summary, 'l1_poll_attempts'),
        'l1_poll_successes': i(summary, 'l1_poll_successes'),
        'l1_poll_success_rate': safe_div(i(summary, 'l1_poll_successes'), i(summary, 'l1_poll_attempts')),
        'moves_per_check': manifest.get('moves_per_check', ''),
        'epsilon_us': manifest.get('epsilon_us', ''),
        'rescue_lookahead_us': manifest.get('rescue_lookahead_us', ''),
        'rescue_idle_pulls_per_check': manifest.get('rescue_idle_pulls_per_check', ''),
        'rescue_distributed_l1_enabled': manifest.get('rescue_distributed_l1_enabled', ''),
        'run_path': str(root),
    }
    return raw, requests, migrations, client_requests, central_ids


def tail_rows(desc, raw, requests, client_requests, central_ids):
    result = []
    server_measurement = [row for row in requests if row['measurement_eligible'] == '1'
                          and row['state'] == 'DONE']
    request_by_id = {int(row['request_id']): row for row in requests}

    def groups_for(rid, row):
        rescued = rid in central_ids
        migrated = i(row, 'migration_count') > 0
        method = row['method']
        return [('all', 'all'), ('method', method),
                ('central_rescue', 'central_rescued' if rescued else 'not_central_rescued'),
                ('migration', 'migrated' if migrated else 'not_migrated'),
                ('method_x_central_rescue', f"{method}|{'central_rescued' if rescued else 'not_central_rescued'}")]

    buckets = defaultdict(list)
    violations = defaultdict(int)
    for row in server_measurement:
        rid = int(row['request_id'])
        for dimension, value in groups_for(rid, row):
            key = ('server_completion_us', dimension, value)
            buckets[key].append(f(row, 'server_completion_us'))
            violations[key] += row['deadline_violation'] == '1'
    for row in client_requests:
        if row.get('measurement_eligible') != '1' or row.get('response_status') != 'received':
            continue
        rid = int(row['request_id'])
        server_row = request_by_id[rid]
        for dimension, value in groups_for(rid, server_row):
            key = ('client_rtt_us', dimension, value)
            buckets[key].append(f(row, 'client_rtt_us'))
            violations[key] += row['server_deadline_violation'] == '1'
    for (metric, dimension, value), values in sorted(buckets.items()):
        result.append({**{key: desc[key] for key in ['scope', 'setup', 'seed', 'variant']},
            'latency_metric': metric, 'dimension': dimension, 'group': value,
            'count': len(values), 'p50_us': quantile(values, .50),
            'p99_us': quantile(values, .99), 'p999_us': quantile(values, .999),
            'deadline_violations': violations[(metric, dimension, value)],
            'deadline_violation_rate': safe_div(violations[(metric, dimension, value)], len(values))})
    return result


def exact_signflip_p(values):
    values = [float(v) for v in values]
    if not values:
        return math.nan
    observed = abs(statistics.mean(values))
    extreme = 0
    total = 0
    for signs in itertools.product((-1.0, 1.0), repeat=len(values)):
        total += 1
        candidate = abs(statistics.mean(v * sign for v, sign in zip(values, signs)))
        if candidate + 1e-15 >= observed:
            extreme += 1
    return extreme / total


def sign_test_p(values):
    positive = sum(v > 0 for v in values)
    negative = sum(v < 0 for v in values)
    n = positive + negative
    if n == 0:
        return 1.0
    k = min(positive, negative)
    tail = sum(math.comb(n, j) for j in range(k + 1)) / (2 ** n)
    return min(1.0, 2.0 * tail)


def bootstrap_ci(values, token):
    values = [float(v) for v in values]
    digest = int(hashlib.sha256(token.encode()).hexdigest()[:16], 16)
    rng = random.Random(BOOTSTRAP_SEED ^ digest)
    samples = []
    for _ in range(BOOTSTRAP_DRAWS):
        samples.append(statistics.mean(rng.choice(values) for _ in values))
    return quantile(samples, .025), quantile(samples, .975)


def cohen_dz(values):
    if len(values) < 2:
        return math.nan
    sd = statistics.stdev(values)
    if sd == 0:
        return math.inf if statistics.mean(values) > 0 else (-math.inf if statistics.mean(values) < 0 else 0.0)
    return statistics.mean(values) / sd


def rank_biserial(values):
    nonzero = [value for value in values if value != 0]
    if not nonzero:
        return 0.0
    return (sum(value > 0 for value in nonzero) - sum(value < 0 for value in nonzero)) / len(nonzero)


def paired_and_aggregate(raw_rows, request_cache):
    by_key = {(row['scope'], row['setup'], int(row['seed']), row['variant']): row for row in raw_rows}
    comparisons = []
    for scope, setup, seed, variant in sorted(by_key):
        variants = {key[3] for key in by_key if key[:3] == (scope, setup, seed)}
        desired = []
        if 'FullHybrid' in variants:
            desired += [('FIFO', 'FullHybrid'), ('M0', 'FullHybrid'), ('L1Only', 'FullHybrid')]
            if 'CentralOnly' in variants:
                desired += [('CentralOnly', 'FullHybrid')]
        if 'CentralOnly' in variants:
            desired += [('FIFO', 'CentralOnly'), ('L1Only', 'CentralOnly')]
        for baseline, candidate in desired:
            if variant != baseline:
                continue
            base = by_key[(scope, setup, seed, baseline)]
            cand = by_key[(scope, setup, seed, candidate)]
            base_requests = request_cache[(scope, setup, seed, baseline)]
            cand_requests = request_cache[(scope, setup, seed, candidate)]
            bmap = {int(row['request_id']): row for row in base_requests
                    if row['measurement_eligible'] == '1'}
            cmap = {int(row['request_id']): row for row in cand_requests
                    if row['measurement_eligible'] == '1'}
            shared = sorted(set(bmap) & set(cmap))
            gross_avoided = sum(bmap[rid]['deadline_violation'] == '1'
                                and cmap[rid]['deadline_violation'] == '0' for rid in shared)
            introduced = sum(bmap[rid]['deadline_violation'] == '0'
                             and cmap[rid]['deadline_violation'] == '1' for rid in shared)
            net_avoided = gross_avoided - introduced
            row = {
                'scope': scope, 'setup': setup, 'seed': seed,
                'comparison': f'{candidate}_vs_{baseline}',
                'baseline': baseline, 'candidate': candidate,
                'baseline_violation_rate': base['deadline_violation_rate'],
                'candidate_violation_rate': cand['deadline_violation_rate'],
                'violation_improvement_pp': 100.0 * (base['deadline_violation_rate'] - cand['deadline_violation_rate']),
                'baseline_slo_goodput_rps': base['slo_goodput_rps'],
                'candidate_slo_goodput_rps': cand['slo_goodput_rps'],
                'slo_goodput_improvement_rps': cand['slo_goodput_rps'] - base['slo_goodput_rps'],
                'baseline_server_p99_us': base['server_p99_us'],
                'candidate_server_p99_us': cand['server_p99_us'],
                'server_p99_improvement_us': base['server_p99_us'] - cand['server_p99_us'],
                'baseline_server_p999_us': base['server_p999_us'],
                'candidate_server_p999_us': cand['server_p999_us'],
                'server_p999_improvement_us': base['server_p999_us'] - cand['server_p999_us'],
                'baseline_client_p99_rtt_us': base['client_p99_rtt_us'],
                'candidate_client_p99_rtt_us': cand['client_p99_rtt_us'],
                'client_p99_rtt_improvement_us': base['client_p99_rtt_us'] - cand['client_p99_rtt_us'],
                'baseline_client_p999_rtt_us': base['client_p999_rtt_us'],
                'candidate_client_p999_rtt_us': cand['client_p999_rtt_us'],
                'client_p999_rtt_improvement_us': base['client_p999_rtt_us'] - cand['client_p999_rtt_us'],
                'client_slo_goodput_improvement_rps': cand['client_slo_goodput_rps'] - base['client_slo_goodput_rps'],
                'baseline_migrations': base['migration_count'], 'candidate_migrations': cand['migration_count'],
                'migration_reduction': base['migration_count'] - cand['migration_count'],
                'baseline_control_ns_per_request': base['control_path_ns_per_request'],
                'candidate_control_ns_per_request': cand['control_path_ns_per_request'],
                'control_overhead_reduction_ns_per_request': base['control_path_ns_per_request'] - cand['control_path_ns_per_request'],
                'baseline_scheduler_lag_us': base['scheduler_max_epoch_lag_us'],
                'candidate_scheduler_lag_us': cand['scheduler_max_epoch_lag_us'],
                'scheduler_lag_reduction_us': base['scheduler_max_epoch_lag_us'] - cand['scheduler_max_epoch_lag_us'],
                'gross_avoided_misses': gross_avoided, 'introduced_misses': introduced,
                'net_avoided_misses': net_avoided,
                'candidate_migrations_per_gross_avoided_miss': safe_div(cand['migration_count'], gross_avoided),
                'candidate_migrations_per_net_avoided_miss': safe_div(cand['migration_count'], net_avoided) if net_avoided > 0 else math.nan,
                'incremental_migrations_per_net_avoided_miss': safe_div(cand['migration_count'] - base['migration_count'], net_avoided) if net_avoided > 0 else math.nan,
                'central_commits_per_net_avoided_miss': safe_div(cand['central_commits'], net_avoided) if net_avoided > 0 else math.nan,
            }
            comparisons.append(row)
    metric_specs = [
        ('deadline_violation_rate', 'baseline_violation_rate', 'candidate_violation_rate', 'violation_improvement_pp', 'percentage_points'),
        ('slo_goodput_rps', 'baseline_slo_goodput_rps', 'candidate_slo_goodput_rps', 'slo_goodput_improvement_rps', 'rps'),
        ('server_p99_us', 'baseline_server_p99_us', 'candidate_server_p99_us', 'server_p99_improvement_us', 'us'),
        ('server_p999_us', 'baseline_server_p999_us', 'candidate_server_p999_us', 'server_p999_improvement_us', 'us'),
        ('client_p99_rtt_us', 'baseline_client_p99_rtt_us', 'candidate_client_p99_rtt_us', 'client_p99_rtt_improvement_us', 'us'),
        ('client_p999_rtt_us', 'baseline_client_p999_rtt_us', 'candidate_client_p999_rtt_us', 'client_p999_rtt_improvement_us', 'us'),
        ('migration_count', 'baseline_migrations', 'candidate_migrations', 'migration_reduction', 'count'),
        ('control_ns_per_request', 'baseline_control_ns_per_request', 'candidate_control_ns_per_request', 'control_overhead_reduction_ns_per_request', 'ns_per_request'),
        ('scheduler_max_lag_us', 'baseline_scheduler_lag_us', 'candidate_scheduler_lag_us', 'scheduler_lag_reduction_us', 'us'),
    ]
    grouped = defaultdict(list)
    for row in comparisons:
        grouped[(row['scope'], row['setup'], row['comparison'])].append(row)
    aggregate = []
    for (scope, setup, comparison), rows in sorted(grouped.items()):
        for metric, bkey, ckey, dkey, unit in metric_specs:
            usable = [row for row in rows if isinstance(row[dkey], (int, float))
                      and math.isfinite(float(row[dkey]))]
            if not usable:
                continue
            deltas = [float(row[dkey]) for row in usable]
            lo, hi = bootstrap_ci(deltas, f'{scope}:{setup}:{comparison}:{metric}')
            aggregate.append({
                'scope': scope, 'setup': setup, 'comparison': comparison,
                'baseline': usable[0]['baseline'], 'candidate': usable[0]['candidate'],
                'metric': metric, 'unit': unit, 'positive_delta_means': 'candidate_better',
                'n_pairs': len(usable), 'baseline_median': statistics.median(float(row[bkey]) for row in usable),
                'candidate_median': statistics.median(float(row[ckey]) for row in usable),
                'paired_mean_delta': statistics.mean(deltas),
                'paired_median_delta': statistics.median(deltas),
                'bootstrap95_mean_delta_low': lo, 'bootstrap95_mean_delta_high': hi,
                'exact_paired_signflip_p_two_sided': exact_signflip_p(deltas),
                'exact_sign_test_p_two_sided': sign_test_p(deltas),
                'cohen_dz': cohen_dz(deltas), 'paired_rank_biserial': rank_biserial(deltas),
                'wins': sum(value > 0 for value in deltas),
                'losses': sum(value < 0 for value in deltas), 'ties': sum(value == 0 for value in deltas),
                'bootstrap_draws': BOOTSTRAP_DRAWS, 'bootstrap_seed': BOOTSTRAP_SEED,
            })
    return comparisons, aggregate


def main():
    raw_rows = []
    tails = []
    request_cache = {}
    validity = []
    descriptors = list(run_descriptors())
    for desc in descriptors:
        raw, requests, migrations, client_requests, central_ids = analyze_run(desc)
        raw_rows.append(raw)
        request_cache[(desc['scope'], desc['setup'], desc['seed'], desc['variant'])] = requests
        tails.extend(tail_rows(desc, raw, requests, client_requests, central_ids))
        validity.append({key: raw[key] for key in ['scope', 'setup', 'seed', 'variant',
            'classification', 'valid', 'validity_errors', 'validity_warnings',
            'decision_records_total',
            'decision_records_sampled', 'decision_records_dropped', 'all_decisions_captured',
            'scheduler_missed_fraction', 'scheduler_max_epoch_lag_us', 'run_path']})
    raw_rows.sort(key=lambda row: (row['scope'], row['setup'], int(row['seed']), row['variant']))
    pairs, aggregate = paired_and_aggregate(raw_rows, request_cache)
    write_csv(OUT / 'raw_metrics.csv', raw_rows)
    write_csv(OUT / 'paired_metrics.csv', pairs)
    write_csv(OUT / 'aggregate.csv', aggregate)
    write_csv(OUT / 'tail_breakdown.csv', tails)
    write_csv(OUT / 'correctness_invariants.csv', validity)
    invalid = [row for row in validity if row['valid'] != 1]
    dropped = [row for row in validity if row['decision_records_dropped'] != 0]
    warned = [row for row in validity if row['validity_warnings']]
    defaults = sorted({(str(row['moves_per_check']), str(row['epsilon_us']),
                        str(row['rescue_lookahead_us']), str(row['rescue_idle_pulls_per_check']))
                       for row in raw_rows if row['variant'] in {'CentralOnly', 'FullHybrid'}})
    counts = Counter(row['scope'] for row in raw_rows)
    with (OUT / 'validation_checks.txt').open('w') as handle:
        handle.write(f'total_runs={len(raw_rows)}\n')
        for scope, count in sorted(counts.items()): handle.write(f'{scope}_runs={count}\n')
        handle.write(f'valid_runs={len(raw_rows)-len(invalid)}\ninvalid_runs={len(invalid)}\n')
        handle.write(f'timing_warning_runs={len(warned)}\n')
        handle.write(f'decision_capture_complete_runs={len(raw_rows)-len(dropped)}\n')
        handle.write(f'decision_capture_dropped_runs={len(dropped)}\n')
        handle.write(f'm1_fixed_parameter_tuples={defaults}\n')
        handle.write(f'paired_rows={len(pairs)}\naggregate_rows={len(aggregate)}\n')
        handle.write(f'bootstrap_draws={BOOTSTRAP_DRAWS}\nbootstrap_seed={BOOTSTRAP_SEED}\n')
        for row in invalid:
            handle.write(f"INVALID {row['scope']} {row['setup']} seed={row['seed']} "
                         f"variant={row['variant']} errors={row['validity_errors']}\n")
        for row in warned:
            handle.write(f"WARNING {row['scope']} {row['setup']} seed={row['seed']} "
                         f"variant={row['variant']} warnings={row['validity_warnings']}\n")
        for row in dropped:
            handle.write(f"DROPPED_DECISIONS {row['scope']} {row['setup']} seed={row['seed']} "
                         f"variant={row['variant']} dropped={row['decision_records_dropped']}\n")
    manifest = {
        'analysis_script': str(Path(__file__).resolve()), 'bootstrap_draws': BOOTSTRAP_DRAWS,
        'bootstrap_seed': BOOTSTRAP_SEED, 'delta_convention': 'positive means candidate better',
        'paired_unit': 'holdout seed', 'holdout_seeds': [71,83,97,109,127],
        'scope_counts': counts, 'run_count': len(raw_rows),
        'notes': ['scheduler CPU is not directly sampled; decision wall duration and cycles are proxies',
                  'loopback RPC uses arrival_scale=10 and check_period_us=100 and is low-load implementation evidence, not formal rho=0.85 evidence',
                  'RPC client send-lag P99 above the original 4-us gate is retained as a non-exclusionary timing warning for every RPC run']}
    (OUT / 'analysis_manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True, default=dict))
    if invalid:
        raise RuntimeError(f'{len(invalid)} invalid runs; see correctness_invariants.csv')

if __name__ == '__main__':
    main()
