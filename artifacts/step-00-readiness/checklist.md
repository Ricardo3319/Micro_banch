# Step-00 Readiness Gate Checklist

> Round date: 2026-03-11
> Source of truth: `微秒级主动迁移调度仿真实验计划.md` and `Flowstep/Flowstep.md`

## A. Frozen Protocol Audit

| ID | Check | Result |
|----|-------|:------:|
| A1 | SLO protocol frozen (short/long + scan points) | PASS |
| A2 | Statistics protocol frozen (warm-up/measurement/seeds/CI) | PASS |
| A3 | Baseline fairness constraints defined | PASS |
| A4 | B2 and M0 rules frozen | PASS |
| A5 | Workloads W1/W2/W3 frozen | PASS |
| A6 | rho->lambda mapping defined | PASS |

## B. Logging/Replay Audit

| ID | Check | Result |
|----|-------|:------:|
| B1 | Minimal logging fields defined in plan | PASS |
| B2 | Replay requirements defined (seed + deterministic order) | PASS |
| B3 | Artifact output convention exists | PASS |

## C. Workspace Structure Audit

| ID | Required Path | Result | Note |
|----|---------------|:------:|------|
| C1 | `d:\桌面图标\Test\src` | PASS | Created |
| C2 | `d:\桌面图标\Test\include` | PASS | Created |
| C3 | `d:\桌面图标\Test\config` | PASS | Created |
| C4 | `d:\桌面图标\Test\scripts` | PASS | Created |
| C5 | `d:\桌面图标\Test\tests` | PASS | Created |
| C6 | `d:\桌面图标\Test\docs` | PASS | Created |
| C7 | Build entry (`main.cpp` + build file) | PASS | Created |
| C8 | Local build toolchain available | FAIL | `cmake/g++/clang++/cl/ninja` not found |

## D. Gate Verdict

`Step-00 = FAIL`

### Blocking Items
1. Local build toolchain is missing.
2. Compile acceptance cannot be completed in current environment.

### Next Action
Install toolchain, run `scripts/run_step00.ps1`, then rerun this checklist.
