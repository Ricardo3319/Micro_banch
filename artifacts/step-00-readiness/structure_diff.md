# Step-00 Structure Diff

## Added Directories
- `src/app`
- `src/core`
- `src/model`
- `src/algorithms`
- `src/workloads`
- `src/metrics`
- `include/sim/core`
- `include/sim/model`
- `include/sim/algorithms`
- `include/sim/workloads`
- `include/sim/metrics`
- `include/sim/common`
- `config`
- `scripts`
- `tests/unit`
- `tests/integration`
- `docs`

## Added Files
- `CMakeLists.txt`
- `src/app/main.cpp`
- `src/core/simulator.cpp`
- `include/sim/core/simulator.h`
- `config/default.yaml`
- `scripts/run_step00.ps1`
- `scripts/run_step01.ps1`
- `docs/ARCHITECTURE.md`
- `docs/MODULE_SPEC.md`

## Validation Notes
- High-cohesion directory skeleton has been created.
- Minimal simulator skeleton code is present.
- Build validation is blocked by missing toolchain (`cmake`, `g++`, `clang++`, `cl`, `ninja` all not found in current environment).
