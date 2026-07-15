# RescueSched Artifact Provenance

This file defines the active evidence chain after removal of the pre-corrected
Step 00-18 experiments. Historical AQB/DQB and older RescueSched outputs are not
paper evidence and are no longer stored in the active tree.

## Evidence directories

| Directory | Role | Paper status |
| --- | --- | --- |
| `artifacts/step-20-corrected-pilot/` | Development seeds, short cohorts | Directional check only |
| `artifacts/step-20b-corrected-holdout-pilot/` | Frozen holdout seeds, short cohorts | Directional check only |
| `artifacts/step-21-corrected-full/` | Ten seeds, full cohorts, paired analysis | Authoritative simulation evidence |

The full result contains raw method rows in `w1.csv`, `w2.csv`, and `w3.csv`;
derived medians in `summary.csv`; paired bootstrap comparisons in
`paired_comparisons.csv`; and the pre-registered decision in `go_no_go.md`.
Its `manifest.md` records the source revision, Linux environment, exact rerun
command, checksums, and claim boundary.

Generated figures are deliberately excluded for now. They can be regenerated
from the authoritative CSVs with `scripts/corrected_eval_plots.py` after the
paper figure plan is frozen.

## Reproduction

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/simulator --mode rescue-smoke

bash scripts/run_corrected_eval.sh pilot build
bash scripts/run_corrected_eval.sh holdout build
bash scripts/run_corrected_eval.sh full build
```

Validate the raw CSV schema:

```bash
python3 tests/integration/validate_rescue_csv_schema.py \
  artifacts/step-21-corrected-full/w1.csv \
  artifacts/step-21-corrected-full/w2.csv \
  artifacts/step-21-corrected-full/w3.csv
```

Re-run only deterministic analysis without simulations:

```bash
python3 scripts/corrected_eval_analysis.py \
  --tier full \
  --out-dir artifacts/step-21-corrected-full \
  --inputs artifacts/step-21-corrected-full/w3.csv \
           artifacts/step-21-corrected-full/w2.csv \
           artifacts/step-21-corrected-full/w1.csv
```

## Physical evidence boundary

`scripts/run_physical_preflight.sh` produces ignored output under
`physical-results/`. It validates a Linux build, CTest, deterministic smoke,
descriptor-handoff microbenchmark stability, a short simulator anchor, schema,
and checksums. It does not implement or validate a real RPC runtime.

No physical result is currently part of the paper evidence chain. That status
changes only after a committed runtime supports frozen trace replay, all four
primary policies, paid descriptor movement, instrumentation, paired runs, and
an auditable physical result manifest.
