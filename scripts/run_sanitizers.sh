#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd -- "$script_dir/.." && pwd)"
asan_build_dir="${1:-build-sanitizers}"
tsan_build_dir="${2:-build-thread-sanitizer}"
if [[ "$asan_build_dir" != /* ]]; then asan_build_dir="$root/$asan_build_dir"; fi
if [[ "$tsan_build_dir" != /* ]]; then tsan_build_dir="$root/$tsan_build_dir"; fi

cmake -S "$root" -B "$asan_build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DRESCUESCHED_ENABLE_SANITIZERS=ON
cmake --build "$asan_build_dir" --parallel
ASAN_OPTIONS="detect_leaks=1:strict_string_checks=1" \
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
    ctest --test-dir "$asan_build_dir" --output-on-failure

echo "ASan/UBSan gate: PASS"

cmake -S "$root" -B "$tsan_build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DRESCUESCHED_ENABLE_THREAD_SANITIZER=ON
cmake --build "$tsan_build_dir" --parallel

set +e
TSAN_OPTIONS="halt_on_error=1:history_size=7" \
    ctest --test-dir "$tsan_build_dir" \
        --output-on-failure -R '^physical_runtime_validity$' \
        2>&1 | tee "$tsan_build_dir/tsan-ctest.log"
tsan_rc=$?
set -e
if (( tsan_rc == 0 )); then
    printf 'status=PASS\nclassification=no_reported_data_race\n' \
        > "$tsan_build_dir/TSAN_STATUS.txt"
    echo "ThreadSanitizer gate: PASS"
elif grep -q 'FATAL: ThreadSanitizer: unexpected memory mapping' \
        "$tsan_build_dir/tsan-ctest.log"; then
    printf '%s\n' \
        'status=UNSUPPORTED' \
        'classification=shadow_memory_mapping_failure_before_test_execution' \
        > "$tsan_build_dir/TSAN_STATUS.txt"
    echo "ThreadSanitizer gate: UNSUPPORTED" >&2
    echo "The runtime test did not execute because this environment rejected" >&2
    echo "ThreadSanitizer shadow-memory mappings." >&2
    exit 77
else
    printf 'status=FAIL\nclassification=test_failure_or_reported_data_race\n' \
        > "$tsan_build_dir/TSAN_STATUS.txt"
    echo "ThreadSanitizer gate: FAIL" >&2
    exit "$tsan_rc"
fi
