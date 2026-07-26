# RescueSched INFOCOM local physical holdout artifact

Start with:

1. `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/FINAL_REPORT.md` — complete findings, regressions, statistics, mechanisms, RPC evidence, and limitations.
2. `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/VALIDATION_SUMMARY.md` — run counts, invariants, tests, and requirement checklist.
3. `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/FAILURE_DIAGNOSIS.md` — preserved failed attempts, root causes, and retained anomalies.

Core machine-readable data:

* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/raw_metrics.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/paired_metrics.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/aggregate.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/bootstrap_95ci.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/setup_variant_summary.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/mechanism_metrics.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/mechanism_summary.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/mechanism_cost_aggregate.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_group_summary.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_policy_comparisons.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/tail_regressions.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/rpc_path_breakdown.csv`
* `/users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/correctness_invariant_detail.csv`

Validation:

```bash
python3 /users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/validate_final_artifact.py
sha256sum -c /users/Mingyang/Micro_banch/artifacts/rescuesched-infocom-physical-holdout-20260726T092143Z/SHA256SUMS
```

Scope is local synthetic runtime plus single-host loopback UDP RPC only. The tree is dirty and uncommitted. No parameter scan, S3, CloudLab, remote host, host tuning, or kernel tuning was performed in this final holdout work.
