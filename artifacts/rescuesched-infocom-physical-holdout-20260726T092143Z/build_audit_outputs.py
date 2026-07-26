#!/usr/bin/env python3
"""Build audit-oriented derivative tables from the completed holdout runs.

This script does not execute benchmarks and does not select parameters. It only reads the
185 retained final runs and the already-produced raw/paired/aggregate/tail CSVs.
"""
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

import numpy as np

ROOT = Path(sys.argv[1]).resolve()
BOOTSTRAP_DRAWS = 10000
BOOTSTRAP_SEED = 2026072603
HOLDOUT_SEEDS = [71, 83, 97, 109, 127]
SMALL_GROUP_THRESHOLD = 100


def read_csv(path):
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def write_csv(path, rows, fieldnames=None):
    rows = list(rows)
    if fieldnames is None:
        if not rows:
            raise RuntimeError(f'cannot infer fields for empty {path}')
        fieldnames = list(rows[0])
    with path.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction='ignore')
        writer.writeheader()
        for row in rows:
            clean = {}
            for key in fieldnames:
                value = row.get(key, '')
                if isinstance(value, float) and not math.isfinite(value):
                    value = ''
                clean[key] = value
            writer.writerow(clean)


def read_kv(path):
    result = {}
    if not path.exists():
        return result
    for line in path.read_text(errors='replace').splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            result[key] = value
    return result


def num(value, default=math.nan):
    if value in ('', None):
        return default
    return float(value)


def integer(value, default=0):
    if value in ('', None):
        return default
    return int(value)


def safe_div(a, b):
    return math.nan if b == 0 else a / b


def quantile(values, q):
    values = sorted(float(v) for v in values)
    if not values:
        return math.nan
    if len(values) == 1:
        return values[0]
    pos = (len(values) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return values[lo]
    frac = pos - lo
    return values[lo] * (1 - frac) + values[hi] * frac


def exact_signflip_p(values):
    values = [float(v) for v in values]
    if not values:
        return math.nan
    observed = abs(statistics.mean(values))
    extreme = 0
    total = 0
    for signs in itertools.product((-1.0, 1.0), repeat=len(values)):
        total += 1
        candidate = abs(statistics.mean(v * s for v, s in zip(values, signs)))
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
    if not values:
        return math.nan, math.nan
    digest = int(hashlib.sha256(token.encode()).hexdigest()[:16], 16)
    rng = np.random.default_rng(BOOTSTRAP_SEED ^ digest)
    array = np.asarray(values, dtype=float)
    indices = rng.integers(0, len(values), size=(BOOTSTRAP_DRAWS, len(values)))
    draws = array[indices].mean(axis=1)
    return float(np.quantile(draws, .025)), float(np.quantile(draws, .975))


def cohen_dz(values):
    if len(values) < 2:
        return math.nan
    sd = statistics.stdev(values)
    mean = statistics.mean(values)
    if sd == 0:
        return math.inf if mean > 0 else (-math.inf if mean < 0 else 0.0)
    return mean / sd


def rank_biserial(values):
    nonzero = [v for v in values if v != 0]
    if not nonzero:
        return 0.0
    return (sum(v > 0 for v in nonzero) - sum(v < 0 for v in nonzero)) / len(nonzero)


def summarize_values(values, token):
    values = [float(v) for v in values if math.isfinite(float(v))]
    lo, hi = bootstrap_ci(values, token)
    return {
        'n': len(values),
        'mean': statistics.mean(values) if values else math.nan,
        'median': statistics.median(values) if values else math.nan,
        'min': min(values) if values else math.nan,
        'max': max(values) if values else math.nan,
        'bootstrap95_mean_low': lo,
        'bootstrap95_mean_high': hi,
    }


def summarize_deltas(values, token):
    values = [float(v) for v in values if math.isfinite(float(v))]
    base = summarize_values(values, token)
    base.update({
        'exact_paired_signflip_p_two_sided': exact_signflip_p(values),
        'exact_sign_test_p_two_sided': sign_test_p(values),
        'cohen_dz': cohen_dz(values),
        'paired_rank_biserial': rank_biserial(values),
        'wins': sum(v > 0 for v in values),
        'losses': sum(v < 0 for v in values),
        'ties': sum(v == 0 for v in values),
    })
    return base


def desired_comparisons(variants):
    desired = []
    if 'FullHybrid' in variants:
        for baseline in ('FIFO', 'M0', 'L1Only'):
            if baseline in variants:
                desired.append((baseline, 'FullHybrid'))
        if 'CentralOnly' in variants:
            desired.append(('CentralOnly', 'FullHybrid'))
    if 'CentralOnly' in variants:
        if 'FIFO' in variants:
            desired.append(('FIFO', 'CentralOnly'))
        if 'L1Only' in variants:
            desired.append(('L1Only', 'CentralOnly'))
    return desired


raw = read_csv(ROOT / 'raw_metrics.csv')
paired = read_csv(ROOT / 'paired_metrics.csv')
aggregate = read_csv(ROOT / 'aggregate.csv')
tails = read_csv(ROOT / 'tail_breakdown.csv')

# 1) Explicit bootstrap/statistical audit table.
bootstrap_rows = []
for r in aggregate:
    lo = num(r['bootstrap95_mean_delta_low'])
    hi = num(r['bootstrap95_mean_delta_high'])
    p = num(r['exact_paired_signflip_p_two_sided'])
    if lo > 0:
        bootstrap_direction = 'candidate_better'
    elif hi < 0:
        bootstrap_direction = 'candidate_worse'
    else:
        bootstrap_direction = 'ci_overlaps_zero'
    bootstrap_rows.append({
        **r,
        'bootstrap95_excludes_zero': int(lo > 0 or hi < 0),
        'bootstrap_direction': bootstrap_direction,
        'exact_signflip_p_lt_0_05': int(p < .05),
        'exact_signflip_alpha05_conclusion': 'reject_null' if p < .05 else 'do_not_reject',
        'n5_minimum_exact_two_sided_p_note': 'minimum_is_0.0625_when_all_5_pairs_align' if integer(r['n_pairs']) == 5 else '',
    })
write_csv(ROOT / 'bootstrap_95ci.csv', bootstrap_rows)

# 2) Same-environment setup/variant summaries.
summary_metrics = [
    'deadline_violation_rate', 'slo_goodput_rps', 'server_p50_us', 'server_p99_us',
    'server_p999_us', 'client_p50_rtt_us', 'client_p99_rtt_us', 'client_p999_rtt_us',
    'client_slo_goodput_rps', 'client_send_lag_p99_us', 'migration_count',
    'migrated_requests', 'repeat_migrations', 'max_migrations_per_request',
    'central_commits', 'central_unique_requests', 'central_rescue_successes',
    'central_rescue_success_rate', 'handoff_p99_ns', 'control_path_ns_per_request',
    'central_scheduler_ns_per_request', 'distributed_l1_ns_per_request',
    'scanned_entries_per_request', 'evaluated_targets_total', 'decision_cycles_total',
    'scheduler_missed_fraction', 'scheduler_max_epoch_lag_us', 'l1_poll_attempts',
    'l1_poll_successes', 'l1_poll_success_rate'
]
setup_variant_rows = []
for (scope, setup, variant), rows in sorted(defaultdict(list, {
        key: [r for r in raw if (r['scope'], r['setup'], r['variant']) == key]
        for key in sorted({(r['scope'], r['setup'], r['variant']) for r in raw})
    }).items()):
    first = rows[0]
    out = {
        'scope': scope, 'setup': setup, 'variant': variant, 'n_seeds': len(rows),
        'seeds': ';'.join(str(x) for x in sorted(integer(r['seed']) for r in rows)),
        'workload': first['workload'], 'rho': first['rho'], 'workers': first['workers'],
        'all_valid': int(all(r['valid'] == '1' for r in rows)),
        'warning_runs': sum(bool(r['validity_warnings']) for r in rows),
    }
    for metric in summary_metrics:
        values = [num(r[metric]) for r in rows if r.get(metric, '') != '']
        if values:
            out[metric + '_mean'] = statistics.mean(values)
            out[metric + '_median'] = statistics.median(values)
            out[metric + '_min'] = min(values)
            out[metric + '_max'] = max(values)
        else:
            for suffix in ('_mean', '_median', '_min', '_max'):
                out[metric + suffix] = ''
    setup_variant_rows.append(out)
write_csv(ROOT / 'setup_variant_summary.csv', setup_variant_rows)

# 3) Outcome result and regression tables.
outcome_metrics = {
    'deadline_violation_rate', 'slo_goodput_rps', 'server_p99_us', 'server_p999_us',
    'client_p99_rtt_us', 'client_p999_rtt_us'
}
key_rows = []
regressions = []
for r in bootstrap_rows:
    if r['metric'] not in outcome_metrics:
        continue
    row = {k: r[k] for k in r}
    mean_delta = num(r['paired_mean_delta'])
    row['mean_direction'] = ('candidate_better' if mean_delta > 0 else
                             'candidate_worse' if mean_delta < 0 else 'tie')
    key_rows.append(row)
    if mean_delta < 0:
        regressions.append(row)
write_csv(ROOT / 'key_results.csv', key_rows)
write_csv(ROOT / 'outcome_regressions.csv', regressions)

# 4) Cross-setup directional generalization (no heterogeneous pooled effect).
gen_rows = []
for (scope, comparison, metric), rows in sorted(defaultdict(list, {
        key: [r for r in bootstrap_rows if (r['scope'], r['comparison'], r['metric']) == key]
        for key in sorted({(r['scope'], r['comparison'], r['metric']) for r in bootstrap_rows})
    }).items()):
    if metric not in outcome_metrics:
        continue
    gen_rows.append({
        'scope': scope, 'comparison': comparison, 'metric': metric,
        'setup_count': len(rows),
        'setups': ';'.join(r['setup'] for r in sorted(rows, key=lambda x: x['setup'])),
        'bootstrap_ci_candidate_better_setups': sum(r['bootstrap_direction'] == 'candidate_better' for r in rows),
        'bootstrap_ci_candidate_worse_setups': sum(r['bootstrap_direction'] == 'candidate_worse' for r in rows),
        'bootstrap_ci_overlap_setups': sum(r['bootstrap_direction'] == 'ci_overlaps_zero' for r in rows),
        'total_seed_pair_wins': sum(integer(r['wins']) for r in rows),
        'total_seed_pair_losses': sum(integer(r['losses']) for r in rows),
        'total_seed_pair_ties': sum(integer(r['ties']) for r in rows),
        'heterogeneous_effects_not_pooled': 1,
    })
write_csv(ROOT / 'generalization_directional_summary.csv', gen_rows)

# 5) Holdout trace/hash and paired-environment audit.
holdout_rows = []
for key in sorted({(r['scope'], r['setup'], integer(r['seed'])) for r in raw}):
    scope, setup, seed = key
    rows = [r for r in raw if (r['scope'], r['setup'], integer(r['seed'])) == key]
    embedded = sorted({r['trace_embedded_sha256'] for r in rows})
    inputs = sorted({r['trace_input_file_sha256'] for r in rows})
    holdout_rows.append({
        'scope': scope, 'setup': setup, 'seed': seed,
        'variants': ';'.join(sorted(r['variant'] for r in rows)),
        'variant_count': len(rows),
        'unique_embedded_trace_hashes': len(embedded),
        'embedded_trace_sha256': ';'.join(embedded),
        'unique_input_trace_hashes': len(inputs),
        'input_trace_sha256': ';'.join(inputs),
        'same_trace_across_variants': int(len(embedded) == 1 and len(inputs) == 1),
        'all_valid': int(all(r['valid'] == '1' for r in rows)),
        'commit_values': ';'.join(sorted({r['commit'] for r in rows})),
        'dirty_values': ';'.join(sorted({r['dirty'] for r in rows})),
        'fixed_m1_tuples': ';'.join(sorted({f"{r['moves_per_check']}/{r['epsilon_us']}/{r['rescue_lookahead_us']}/{r['rescue_idle_pulls_per_check']}" for r in rows})),
    })
write_csv(ROOT / 'holdout_trace_audit.csv', holdout_rows)

# 6) Per-run mechanism metrics and response-queue status.
paired_map = {(r['scope'], r['setup'], integer(r['seed']), r['candidate'], r['baseline']): r
              for r in paired}
mechanism_rows = []
for r in raw:
    root = Path(r['run_path'])
    server_status = read_kv(root / 'RPC_SERVER_STATUS.txt') if r['scope'] == 'loopback_udp_rpc' else {}
    migration_count = integer(r['migration_count'])
    migrated_requests = integer(r['migrated_requests'])
    repeats = integer(r['repeat_migrations'])
    central_commits = integer(r['central_commits'])
    central_successes = integer(r['central_rescue_successes'])
    decisions = integer(r['decision_records_total'])
    row = {k: r[k] for k in ['scope', 'setup', 'seed', 'variant', 'workload', 'rho', 'workers',
                                       'commit', 'dirty', 'valid', 'validity_warnings', 'run_path']}
    row.update({
        'migration_count': migration_count,
        'migrated_requests': migrated_requests,
        'migrations_per_migrated_request': safe_div(migration_count, migrated_requests),
        'repeat_migrations': repeats,
        'repeat_migration_fraction_of_migrations': safe_div(repeats, migration_count),
        'max_migrations_per_request': integer(r['max_migrations_per_request']),
        'central_commits': central_commits,
        'central_unique_requests': integer(r['central_unique_requests']),
        'central_rescue_successes': central_successes,
        'central_rescue_success_rate': num(r['central_rescue_success_rate']),
        'moved_estimated_work_us': num(r['moved_estimated_work_us'], 0),
        'moved_estimated_work_us_per_migration': safe_div(num(r['moved_estimated_work_us'], 0), migration_count),
        'handoff_total_ns': integer(r['handoff_total_ns']),
        'handoff_p99_ns': num(r['handoff_p99_ns']),
        'control_path_total_duration_ns': integer(r['control_path_total_duration_ns']),
        'control_path_ns_per_request': num(r['control_path_ns_per_request'], 0),
        'central_scheduler_ns_per_request': num(r['central_scheduler_ns_per_request'], 0),
        'distributed_l1_ns_per_request': num(r['distributed_l1_ns_per_request'], 0),
        'decision_records_total': decisions,
        'decision_cycles_total': integer(r['decision_cycles_total']),
        'decision_cycles_per_request': safe_div(integer(r['decision_cycles_total']), integer(r['measurement_requests'])),
        'control_path_ns_per_decision_record': safe_div(integer(r['control_path_total_duration_ns']), decisions),
        'scanned_entries_total': integer(r['scanned_entries_total']),
        'scanned_entries_per_request': num(r['scanned_entries_per_request'], 0),
        'scanned_entries_per_decision_record': safe_div(integer(r['scanned_entries_total']), decisions),
        'evaluated_targets_total': integer(r['evaluated_targets_total']),
        'evaluated_targets_per_decision_record': safe_div(integer(r['evaluated_targets_total']), decisions),
        'scheduler_epochs_scheduled': integer(r['scheduler_epochs_scheduled']),
        'scheduler_epochs_executed': integer(r['scheduler_epochs_executed']),
        'scheduler_epochs_missed': integer(r['scheduler_epochs_missed']),
        'scheduler_missed_fraction': num(r['scheduler_missed_fraction']),
        'scheduler_max_epoch_lag_us': num(r['scheduler_max_epoch_lag_us'], 0),
        'l1_poll_attempts': integer(r['l1_poll_attempts']),
        'l1_poll_successes': integer(r['l1_poll_successes']),
        'l1_poll_success_rate': num(r['l1_poll_success_rate']),
        'response_queue_high_watermark': integer(server_status.get('response_queue_high_watermark', ''), 0) if server_status else '',
        'response_queue_capacity': integer(server_status.get('response_queue_capacity', ''), 0) if server_status else '',
        'scheduler_thread_cpu_time_directly_measured': 0,
        'scheduler_cpu_note': 'decision duration/cycles are wall/cycle proxies, not scheduler thread CPU time',
    })
    if r['variant'] != 'FIFO':
        pr = paired_map.get((r['scope'], r['setup'], integer(r['seed']), r['variant'], 'FIFO'))
        if pr:
            for field in ['gross_avoided_misses', 'introduced_misses', 'net_avoided_misses',
                          'candidate_migrations_per_gross_avoided_miss',
                          'candidate_migrations_per_net_avoided_miss',
                          'incremental_migrations_per_net_avoided_miss',
                          'central_commits_per_net_avoided_miss']:
                row[field + '_vs_FIFO'] = pr[field]
    mechanism_rows.append(row)
write_csv(ROOT / 'mechanism_metrics.csv', mechanism_rows)

mechanism_summary_metrics = [
    'migration_count', 'migrated_requests', 'migrations_per_migrated_request',
    'repeat_migrations', 'repeat_migration_fraction_of_migrations', 'max_migrations_per_request',
    'central_commits', 'central_unique_requests', 'central_rescue_successes',
    'central_rescue_success_rate', 'moved_estimated_work_us_per_migration', 'handoff_p99_ns',
    'control_path_ns_per_request', 'central_scheduler_ns_per_request',
    'distributed_l1_ns_per_request', 'decision_cycles_per_request',
    'control_path_ns_per_decision_record', 'scanned_entries_per_request',
    'scanned_entries_per_decision_record', 'evaluated_targets_per_decision_record',
    'scheduler_missed_fraction', 'scheduler_max_epoch_lag_us', 'l1_poll_attempts',
    'l1_poll_successes', 'l1_poll_success_rate', 'response_queue_high_watermark'
]
mechanism_summary = []
for key in sorted({(r['scope'], r['setup'], r['variant']) for r in mechanism_rows}):
    scope, setup, variant = key
    rows = [r for r in mechanism_rows if (r['scope'], r['setup'], r['variant']) == key]
    for metric in mechanism_summary_metrics:
        values = [num(r.get(metric, '')) for r in rows if r.get(metric, '') not in ('', None)]
        values = [v for v in values if math.isfinite(v)]
        if not values:
            continue
        stats = summarize_values(values, f'mechanism|{scope}|{setup}|{variant}|{metric}')
        mechanism_summary.append({
            'scope': scope, 'setup': setup, 'variant': variant, 'metric': metric,
            **stats, 'bootstrap_draws': BOOTSTRAP_DRAWS, 'bootstrap_seed': BOOTSTRAP_SEED,
            'scheduler_cpu_direct_measurement': 0,
        })
write_csv(ROOT / 'mechanism_summary.csv', mechanism_summary)

# 7) Comparison-level avoided-miss / migration-cost statistics.
cost_fields = {
    'gross_avoided_misses': 'requests',
    'introduced_misses': 'requests',
    'net_avoided_misses': 'requests',
    'candidate_migrations_per_gross_avoided_miss': 'migrations_per_request',
    'candidate_migrations_per_net_avoided_miss': 'migrations_per_request',
    'incremental_migrations_per_net_avoided_miss': 'migrations_per_request',
    'central_commits_per_net_avoided_miss': 'commits_per_request',
}
cost_rows = []
for key in sorted({(r['scope'], r['setup'], r['comparison']) for r in paired}):
    scope, setup, comparison = key
    rows = [r for r in paired if (r['scope'], r['setup'], r['comparison']) == key]
    for field, unit in cost_fields.items():
        values = [num(r[field]) for r in rows if r[field] != '']
        values = [v for v in values if math.isfinite(v)]
        if not values:
            continue
        stats = summarize_deltas(values, f'cost|{scope}|{setup}|{comparison}|{field}')
        cost_rows.append({
            'scope': scope, 'setup': setup, 'comparison': comparison,
            'baseline': rows[0]['baseline'], 'candidate': rows[0]['candidate'],
            'metric': field, 'unit': unit,
            'positive_delta_means': ('more_avoided_misses' if field in {'gross_avoided_misses', 'net_avoided_misses'}
                                     else 'more_introduced_misses' if field == 'introduced_misses'
                                     else 'higher_cost'),
            **stats,
            'available_seed_pairs': ';'.join(r['seed'] for r in rows if r[field] != ''),
            'missing_due_to_nonpositive_net_avoided': len(rows) - len(values) if 'net_avoided' in field else 0,
            'bootstrap_draws': BOOTSTRAP_DRAWS, 'bootstrap_seed': BOOTSTRAP_SEED,
        })
write_csv(ROOT / 'mechanism_cost_aggregate.csv', cost_rows)

# 8) Real loopback RPC path decomposition, including per-request residual.
rpc_breakdown = []
rpc_class_breakdown = []
for r in raw:
    if r['scope'] != 'loopback_udp_rpc':
        continue
    server_dir = Path(r['run_path'])
    run = server_dir.parent
    server_rows = read_csv(server_dir / 'requests.csv')
    client_rows = read_csv(run / 'client' / 'client_requests.csv')
    decisions = read_csv(server_dir / 'decisions.csv')
    central_ids = {integer(d['request_id']) for d in decisions
                   if d.get('reason') == 'commit_local_doom_remote_feasible' and integer(d['request_id']) > 0}
    smap = {integer(x['request_id']): x for x in server_rows}
    records = []
    for c in client_rows:
        rid = integer(c['request_id'])
        if c.get('measurement_eligible') != '1' or c.get('response_status') != 'received' or rid not in smap:
            continue
        s = smap[rid]
        client_rtt = num(c['client_rtt_us'])
        server_completion = num(s['server_completion_us'])
        records.append({
            'request_id': rid,
            'client_rtt_us': client_rtt,
            'server_completion_us': server_completion,
            'non_runtime_residual_us': client_rtt - server_completion,
            'method': s['method'],
            'migrated': integer(s['migration_count']) > 0,
            'central_rescued': rid in central_ids,
        })
    status = read_kv(server_dir / 'RPC_SERVER_STATUS.txt')
    def q(field, p):
        return quantile([x[field] for x in records], p)
    rpc_breakdown.append({
        'scope': r['scope'], 'setup': r['setup'], 'seed': r['seed'], 'variant': r['variant'],
        'measurement_requests': len(records),
        'client_rtt_p50_us': q('client_rtt_us', .50),
        'client_rtt_p99_us': q('client_rtt_us', .99),
        'client_rtt_p999_us': q('client_rtt_us', .999),
        'server_completion_p50_us': q('server_completion_us', .50),
        'server_completion_p99_us': q('server_completion_us', .99),
        'server_completion_p999_us': q('server_completion_us', .999),
        'non_runtime_residual_p50_us': q('non_runtime_residual_us', .50),
        'non_runtime_residual_p99_us': q('non_runtime_residual_us', .99),
        'non_runtime_residual_p999_us': q('non_runtime_residual_us', .999),
        'non_runtime_residual_max_us': max(x['non_runtime_residual_us'] for x in records),
        'response_queue_high_watermark': integer(status.get('response_queue_high_watermark', '')),
        'response_queue_capacity': integer(status.get('response_queue_capacity', '')),
        'response_send_failures': integer(status.get('response_send_failures', '')),
        'response_enqueue_failures': integer(status.get('response_enqueue_failures', '')),
        'l1_poll_attempts': r['l1_poll_attempts'],
        'l1_poll_successes': r['l1_poll_successes'],
        'l1_poll_success_rate': r['l1_poll_success_rate'],
        'control_path_ns_per_request': r['control_path_ns_per_request'],
        'distributed_l1_ns_per_request': r['distributed_l1_ns_per_request'],
        'scheduler_missed_fraction': r['scheduler_missed_fraction'],
        'scheduler_max_epoch_lag_us': r['scheduler_max_epoch_lag_us'],
        'client_send_lag_p99_us': r['client_send_lag_p99_us'],
        'validity_warnings': r['validity_warnings'],
        'run_path': r['run_path'],
    })
    buckets = defaultdict(list)
    for x in records:
        labels = [
            ('all', 'all'), ('method', x['method']),
            ('migration', 'migrated' if x['migrated'] else 'not_migrated'),
            ('central_rescue', 'central_rescued' if x['central_rescued'] else 'not_central_rescued'),
            ('method_x_central_rescue', f"{x['method']}|{'central_rescued' if x['central_rescued'] else 'not_central_rescued'}"),
        ]
        for label in labels:
            buckets[label].append(x['non_runtime_residual_us'])
    for (dimension, group), values in sorted(buckets.items()):
        rpc_class_breakdown.append({
            'scope': r['scope'], 'setup': r['setup'], 'seed': r['seed'], 'variant': r['variant'],
            'dimension': dimension, 'group': group, 'count': len(values),
            'non_runtime_residual_p50_us': quantile(values, .50),
            'non_runtime_residual_p99_us': quantile(values, .99),
            'non_runtime_residual_p999_us': quantile(values, .999),
            'small_group_warning': int(len(values) < SMALL_GROUP_THRESHOLD),
            'interpretation': 'client RTT minus server completion; includes pre-submit ingress wait plus loopback/response path, not a direct mutex-wait sample',
        })
write_csv(ROOT / 'rpc_path_breakdown.csv', rpc_breakdown)
write_csv(ROOT / 'rpc_path_breakdown_by_class.csv', rpc_class_breakdown)

rpc_variant_summary = []
for variant in sorted({r['variant'] for r in rpc_breakdown}):
    rows = [r for r in rpc_breakdown if r['variant'] == variant]
    for metric in ['client_rtt_p50_us', 'client_rtt_p99_us', 'client_rtt_p999_us',
                   'server_completion_p50_us', 'server_completion_p99_us', 'server_completion_p999_us',
                   'non_runtime_residual_p50_us', 'non_runtime_residual_p99_us',
                   'non_runtime_residual_p999_us', 'response_queue_high_watermark',
                   'l1_poll_attempts', 'l1_poll_success_rate', 'control_path_ns_per_request',
                   'distributed_l1_ns_per_request', 'scheduler_missed_fraction',
                   'scheduler_max_epoch_lag_us', 'client_send_lag_p99_us']:
        values = [num(r[metric]) for r in rows if r.get(metric, '') != '']
        stats = summarize_values(values, f'rpc|{variant}|{metric}')
        rpc_variant_summary.append({
            'variant': variant, 'metric': metric, **stats,
            'bootstrap_draws': BOOTSTRAP_DRAWS, 'bootstrap_seed': BOOTSTRAP_SEED,
        })
write_csv(ROOT / 'rpc_variant_summary.csv', rpc_variant_summary)

# 9) Tail group summaries and policy comparisons.
tail_group_summary = []
for key in sorted({(r['scope'], r['setup'], r['variant'], r['latency_metric'], r['dimension'], r['group']) for r in tails}):
    scope, setup, variant, latency_metric, dimension, group = key
    rows = [r for r in tails if (r['scope'], r['setup'], r['variant'], r['latency_metric'], r['dimension'], r['group']) == key]
    p99s = [num(r['p99_us']) for r in rows]
    p999s = [num(r['p999_us']) for r in rows]
    counts = [integer(r['count']) for r in rows]
    rates = [num(r['deadline_violation_rate']) for r in rows]
    s99 = summarize_values(p99s, f'tail99|{key}')
    s999 = summarize_values(p999s, f'tail999|{key}')
    tail_group_summary.append({
        'scope': scope, 'setup': setup, 'variant': variant,
        'latency_metric': latency_metric, 'dimension': dimension, 'group': group,
        'n_seeds_present': len(rows), 'seeds': ';'.join(sorted((r['seed'] for r in rows), key=int)),
        'count_median': statistics.median(counts), 'count_min': min(counts), 'count_max': max(counts),
        'p99_mean_us': s99['mean'], 'p99_median_us': s99['median'],
        'p99_bootstrap95_mean_low_us': s99['bootstrap95_mean_low'],
        'p99_bootstrap95_mean_high_us': s99['bootstrap95_mean_high'],
        'p999_mean_us': s999['mean'], 'p999_median_us': s999['median'],
        'p999_bootstrap95_mean_low_us': s999['bootstrap95_mean_low'],
        'p999_bootstrap95_mean_high_us': s999['bootstrap95_mean_high'],
        'deadline_violation_rate_mean': statistics.mean(rates),
        'deadline_violation_rate_median': statistics.median(rates),
        'small_group_warning': int(min(counts) < SMALL_GROUP_THRESHOLD),
        'selection_warning': int(dimension in {'central_rescue', 'migration', 'method_x_central_rescue'}),
    })
write_csv(ROOT / 'tail_group_summary.csv', tail_group_summary)

tail_map = {(r['scope'], r['setup'], integer(r['seed']), r['variant'], r['latency_metric'], r['dimension'], r['group']): r
            for r in tails}
tail_comparisons = []
for scope, setup, latency_metric, dimension, group in sorted({
        (r['scope'], r['setup'], r['latency_metric'], r['dimension'], r['group']) for r in tails}):
    variants = {r['variant'] for r in tails if r['scope'] == scope and r['setup'] == setup}
    for baseline, candidate in desired_comparisons(variants):
        seed_rows = []
        for seed in HOLDOUT_SEEDS:
            bk = (scope, setup, seed, baseline, latency_metric, dimension, group)
            ck = (scope, setup, seed, candidate, latency_metric, dimension, group)
            if bk in tail_map and ck in tail_map:
                seed_rows.append((seed, tail_map[bk], tail_map[ck]))
        if not seed_rows:
            continue
        for quant in ('p99_us', 'p999_us'):
            deltas = [num(b[quant]) - num(c[quant]) for _, b, c in seed_rows]
            stats = summarize_deltas(deltas, f'tailcmp|{scope}|{setup}|{baseline}|{candidate}|{latency_metric}|{dimension}|{group}|{quant}')
            min_count = min(min(integer(b['count']), integer(c['count'])) for _, b, c in seed_rows)
            tail_comparisons.append({
                'scope': scope, 'setup': setup,
                'comparison': f'{candidate}_vs_{baseline}', 'baseline': baseline, 'candidate': candidate,
                'latency_metric': latency_metric, 'dimension': dimension, 'group': group,
                'quantile': quant.replace('_us', ''), 'unit': 'us',
                'positive_delta_means': 'candidate_lower_tail',
                'paired_seeds': ';'.join(str(seed) for seed, _, _ in seed_rows),
                'minimum_group_count_in_any_pair': min_count,
                'small_group_warning': int(min_count < SMALL_GROUP_THRESHOLD),
                'population_comparability': ('same_trace_request_class' if dimension in {'all', 'method'}
                                              else 'policy_selected_subpopulation_descriptive_not_causal'),
                **stats, 'bootstrap_draws': BOOTSTRAP_DRAWS, 'bootstrap_seed': BOOTSTRAP_SEED,
            })
write_csv(ROOT / 'tail_policy_comparisons.csv', tail_comparisons)
write_csv(ROOT / 'tail_regressions.csv', [r for r in tail_comparisons if num(r['mean']) < 0])

# Within-policy descriptive contrasts.
contrast_specs = [
    ('central_rescue', 'central_rescued', 'not_central_rescued', 'rescued_minus_nonrescued'),
    ('migration', 'migrated', 'not_migrated', 'migrated_minus_nonmigrated'),
    ('method', 'long', 'short', 'long_minus_short'),
]
within_rows = []
for scope, setup, variant, latency_metric in sorted({
        (r['scope'], r['setup'], r['variant'], r['latency_metric']) for r in tails}):
    for dimension, first_group, second_group, contrast in contrast_specs:
        paired_groups = []
        for seed in HOLDOUT_SEEDS:
            fk = (scope, setup, seed, variant, latency_metric, dimension, first_group)
            sk = (scope, setup, seed, variant, latency_metric, dimension, second_group)
            if fk in tail_map and sk in tail_map:
                paired_groups.append((seed, tail_map[fk], tail_map[sk]))
        if not paired_groups:
            continue
        for quant in ('p99_us', 'p999_us'):
            deltas = [num(a[quant]) - num(b[quant]) for _, a, b in paired_groups]
            stats = summarize_deltas(deltas, f'within|{scope}|{setup}|{variant}|{latency_metric}|{contrast}|{quant}')
            min_count = min(min(integer(a['count']), integer(b['count'])) for _, a, b in paired_groups)
            within_rows.append({
                'scope': scope, 'setup': setup, 'variant': variant,
                'latency_metric': latency_metric, 'dimension': dimension,
                'contrast': contrast, 'first_group': first_group, 'second_group': second_group,
                'quantile': quant.replace('_us', ''),
                'positive_delta_means': 'first_group_has_higher_tail',
                'paired_seeds': ';'.join(str(seed) for seed, _, _ in paired_groups),
                'minimum_group_count': min_count,
                'small_group_warning': int(min_count < SMALL_GROUP_THRESHOLD),
                'selection_warning': int(dimension in {'central_rescue', 'migration'}),
                **stats, 'bootstrap_draws': BOOTSTRAP_DRAWS, 'bootstrap_seed': BOOTSTRAP_SEED,
            })
write_csv(ROOT / 'within_variant_tail_contrasts.csv', within_rows)

# 10) Focused worker/load table, preserving per-setup estimates rather than pooling.
worker_rows = []
for r in bootstrap_rows:
    if r['scope'] != 'local_synthetic_runtime' or r['comparison'] not in {
            'FullHybrid_vs_FIFO', 'FullHybrid_vs_M0', 'FullHybrid_vs_L1Only'}:
        continue
    if r['metric'] not in {'deadline_violation_rate', 'slo_goodput_rps', 'server_p99_us', 'server_p999_us'}:
        continue
    raw_match = next(x for x in raw if x['scope'] == r['scope'] and x['setup'] == r['setup'])
    worker_rows.append({
        'workload': raw_match['workload'], 'rho': raw_match['rho'], 'workers': raw_match['workers'],
        **r,
    })
write_csv(ROOT / 'worker_load_summary.csv', worker_rows)

# 11) Expanded correctness/network invariant audit, one row per retained run.
correctness_detail = []
for r in raw:
    run_path = Path(r['run_path'])
    summary_path = run_path / 'summary.csv'
    summary_rows = read_csv(summary_path)
    if len(summary_rows) != 1:
        raise RuntimeError(f'expected one summary row in {summary_path}')
    sm = summary_rows[0]
    runtime_status = read_kv(run_path / 'RUNTIME_STATUS.txt')
    manifest = read_kv(run_path / 'manifest.env')
    rpc_server_status = read_kv(run_path / 'RPC_SERVER_STATUS.txt') if r['scope'] == 'loopback_udp_rpc' else {}
    rpc_client_status = read_kv(run_path.parent / 'client' / 'RPC_CLIENT_STATUS.txt') if r['scope'] == 'loopback_udp_rpc' else {}
    total_requests = integer(sm.get('total_requests', ''))
    completed_requests = integer(sm.get('completed_requests', ''))
    invariant_zero_fields = [
        'duplicate_execution_count', 'duplicate_completion_count', 'lost_descriptor_count',
        'nonzero_reservation_count', 'affinity_failure_count'
    ]
    zero_ok = all(integer(sm.get(k, '')) == 0 for k in invariant_zero_fields)
    basic_ok = (
        sm.get('classification') == 'VALID_COMPLETE'
        and sm.get('invariants_pass') == '1'
        and total_requests == completed_requests
        and integer(sm.get('cancelled_requests', '')) == 0
        and zero_ok
        and runtime_status.get('status') == 'PASS'
        and integer(r['decision_records_dropped']) == 0
        and r['all_decisions_captured'] == '1'
        and manifest.get('moves_per_check') == '4'
        and num(manifest.get('epsilon_us', '')) == 6.0
        and num(manifest.get('rescue_lookahead_us', '')) == 20.0
        and manifest.get('rescue_idle_pulls_per_check') == '0'
    )
    rpc_ok = True
    if r['scope'] == 'loopback_udp_rpc':
        server_zero = ['invalid_packets', 'duplicate_packets', 'unknown_requests',
                       'flow_mismatch_requests', 'response_enqueue_failures',
                       'response_send_failures', 'receiver_internal_failures']
        client_zero = ['timeouts', 'send_failures', 'invalid_responses', 'duplicate_responses']
        rpc_ok = (
            rpc_server_status.get('status') == 'PASS'
            and rpc_server_status.get('classification') == 'VALID_COMPLETE'
            and all(integer(rpc_server_status.get(k, '')) == 0 for k in server_zero)
            and integer(rpc_server_status.get('expected_requests', '')) == total_requests
            and integer(rpc_server_status.get('accepted_requests', '')) == total_requests
            and integer(rpc_server_status.get('responses_enqueued', '')) == total_requests
            and integer(rpc_server_status.get('responses_sent', '')) == total_requests
            and all(rpc_server_status.get(k) == '1' for k in [
                'receiver_affinity_pass', 'response_sender_affinity_pass',
                'scheduler_affinity_pass', 'server_main_affinity_pass', 'runtime_invariants_pass'])
            and rpc_client_status.get('status') == 'PASS'
            and all(integer(rpc_client_status.get(k, '')) == 0 for k in client_zero)
            and integer(rpc_client_status.get('total_requests', '')) == total_requests
            and integer(rpc_client_status.get('responses', '')) == total_requests
            and rpc_client_status.get('sender_affinity_pass') == '1'
            and rpc_client_status.get('receiver_affinity_pass') == '1'
            and manifest.get('check_period_us') == '100'
        )
    correctness_detail.append({
        'scope': r['scope'], 'setup': r['setup'], 'seed': r['seed'], 'variant': r['variant'],
        'classification': sm.get('classification', ''),
        'runtime_status': runtime_status.get('status', ''),
        'invariants_pass': sm.get('invariants_pass', ''),
        'total_requests': total_requests,
        'measurement_requests': integer(sm.get('measurement_requests', '')),
        'completed_requests': completed_requests,
        'cancelled_requests': integer(sm.get('cancelled_requests', '')),
        'duplicate_execution_count': integer(sm.get('duplicate_execution_count', '')),
        'duplicate_completion_count': integer(sm.get('duplicate_completion_count', '')),
        'lost_descriptor_count': integer(sm.get('lost_descriptor_count', '')),
        'nonzero_reservation_count': integer(sm.get('nonzero_reservation_count', '')),
        'affinity_failure_count': integer(sm.get('affinity_failure_count', '')),
        'scheduler_affinity_ok': sm.get('scheduler_affinity_ok', ''),
        'decision_records_total': r['decision_records_total'],
        'decision_records_sampled': r['decision_records_sampled'],
        'decision_records_dropped': r['decision_records_dropped'],
        'all_decisions_captured': r['all_decisions_captured'],
        'scheduler_missed_fraction': r['scheduler_missed_fraction'],
        'scheduler_max_epoch_lag_us': r['scheduler_max_epoch_lag_us'],
        'expected_requests': rpc_server_status.get('expected_requests', ''),
        'accepted_requests': rpc_server_status.get('accepted_requests', ''),
        'responses_enqueued': rpc_server_status.get('responses_enqueued', ''),
        'responses_sent': rpc_server_status.get('responses_sent', ''),
        'invalid_packets': rpc_server_status.get('invalid_packets', ''),
        'duplicate_packets': rpc_server_status.get('duplicate_packets', ''),
        'unknown_requests': rpc_server_status.get('unknown_requests', ''),
        'flow_mismatch_requests': rpc_server_status.get('flow_mismatch_requests', ''),
        'response_enqueue_failures': rpc_server_status.get('response_enqueue_failures', ''),
        'response_send_failures': rpc_server_status.get('response_send_failures', ''),
        'receiver_internal_failures': rpc_server_status.get('receiver_internal_failures', ''),
        'receiver_affinity_pass': rpc_server_status.get('receiver_affinity_pass', ''),
        'response_sender_affinity_pass': rpc_server_status.get('response_sender_affinity_pass', ''),
        'server_main_affinity_pass': rpc_server_status.get('server_main_affinity_pass', ''),
        'client_responses': rpc_client_status.get('responses', ''),
        'client_timeouts': rpc_client_status.get('timeouts', ''),
        'client_send_failures': rpc_client_status.get('send_failures', ''),
        'client_invalid_responses': rpc_client_status.get('invalid_responses', ''),
        'client_duplicate_responses': rpc_client_status.get('duplicate_responses', ''),
        'client_sender_affinity_pass': rpc_client_status.get('sender_affinity_pass', ''),
        'client_receiver_affinity_pass': rpc_client_status.get('receiver_affinity_pass', ''),
        'moves_per_check': manifest.get('moves_per_check', ''),
        'epsilon_us': manifest.get('epsilon_us', ''),
        'rescue_lookahead_us': manifest.get('rescue_lookahead_us', ''),
        'rescue_idle_pulls_per_check': manifest.get('rescue_idle_pulls_per_check', ''),
        'rescue_distributed_l1_enabled': manifest.get('rescue_distributed_l1_enabled', ''),
        'validity_warnings': r['validity_warnings'],
        'correctness_audit_pass': int(basic_ok and rpc_ok),
        'run_path': r['run_path'],
    })
write_csv(ROOT / 'correctness_invariant_detail.csv', correctness_detail)

# 12) Derived-output manifest.
derived_files = [
    'bootstrap_95ci.csv', 'setup_variant_summary.csv', 'key_results.csv',
    'outcome_regressions.csv', 'generalization_directional_summary.csv',
    'holdout_trace_audit.csv', 'mechanism_metrics.csv', 'mechanism_summary.csv',
    'mechanism_cost_aggregate.csv', 'rpc_path_breakdown.csv',
    'rpc_path_breakdown_by_class.csv', 'rpc_variant_summary.csv',
    'tail_group_summary.csv', 'tail_policy_comparisons.csv', 'tail_regressions.csv',
    'within_variant_tail_contrasts.csv', 'worker_load_summary.csv',
    'correctness_invariant_detail.csv'
]
manifest = {
    'script': str(Path(__file__).resolve()),
    'input_run_count': len(raw),
    'input_paired_rows': len(paired),
    'input_aggregate_rows': len(aggregate),
    'input_tail_rows': len(tails),
    'holdout_seeds': HOLDOUT_SEEDS,
    'bootstrap_draws': BOOTSTRAP_DRAWS,
    'bootstrap_seed': BOOTSTRAP_SEED,
    'small_group_threshold': SMALL_GROUP_THRESHOLD,
    'benchmark_execution': False,
    'parameter_selection_or_scan': False,
    'scheduler_cpu_directly_measured': False,
    'generated_files': derived_files,
    'notes': [
        'positive paired outcome delta means candidate better unless a row explicitly says otherwise',
        'tail migration/rescue groups are policy-selected subpopulations and are descriptive, not causal',
        'RPC non_runtime_residual is per-request client RTT minus server completion and is not a direct mutex-wait measurement',
        'heterogeneous setups are directionally summarized but not pooled into a universal effect',
    ],
}
(ROOT / 'derived_analysis_manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True))

print(f'generated {len(derived_files)} CSV files from {len(raw)} retained final runs')
