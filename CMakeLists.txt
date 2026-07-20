cmake_minimum_required(VERSION 3.16)
project(micro_migration_sim LANGUAGES CXX)

option(RESCUESCHED_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RESCUESCHED_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(RESCUESCHED_ENABLE_SANITIZERS AND RESCUESCHED_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sim_core
    src/core/simulator.cpp
    src/core/workload_trace.cpp
)

target_include_directories(sim_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)

add_library(physical_runtime_core
    src/physical/runtime.cpp
    src/physical/runtime_support.cpp
    src/physical/trace.cpp
)
target_include_directories(physical_runtime_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(physical_runtime_core PUBLIC sim_core Threads::Threads)

add_executable(rescuesched_trace_generator
    src/app/trace_generator_main.cpp
)
target_link_libraries(rescuesched_trace_generator PRIVATE sim_core)

# Enable optimizations for the simulation workload.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(sim_core PRIVATE -O2)
endif()

add_executable(simulator
    src/app/main.cpp
)

target_link_libraries(simulator PRIVATE sim_core)

add_executable(simulator_validity_tests
    tests/unit/simulator_validity_tests.cpp
)
target_link_libraries(simulator_validity_tests PRIVATE sim_core)

add_executable(rescuesched_physical_runtime
    src/app/physical_runtime_main.cpp
)
target_link_libraries(rescuesched_physical_runtime PRIVATE physical_runtime_core)

add_executable(physical_runtime_tests
    tests/unit/physical_runtime_tests.cpp
)
target_link_libraries(physical_runtime_tests PRIVATE physical_runtime_core)

add_executable(wp2_runtime_support_tests
    tests/unit/wp2_runtime_support_tests.cpp
)
target_link_libraries(wp2_runtime_support_tests PRIVATE physical_runtime_core)

add_executable(wp2_runtime_concurrency_tests
    tests/unit/wp2_runtime_concurrency_tests.cpp
)
target_link_libraries(wp2_runtime_concurrency_tests PRIVATE physical_runtime_core)

add_executable(rescuesched_handoff_microbench
    src/app/handoff_microbench_main.cpp
)
target_link_libraries(rescuesched_handoff_microbench PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(rescuesched_rpc_server
        src/app/rpc_server_main.cpp
    )
    target_link_libraries(rescuesched_rpc_server PRIVATE physical_runtime_core)

    add_executable(rescuesched_rpc_client
        src/app/rpc_client_main.cpp
    )
    target_link_libraries(rescuesched_rpc_client PRIVATE physical_runtime_core)

    add_executable(rpc_protocol_tests
        tests/unit/rpc_protocol_tests.cpp
    )
    target_link_libraries(rpc_protocol_tests PRIVATE physical_runtime_core)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(simulator PRIVATE -O2)
    target_compile_options(simulator_validity_tests PRIVATE -O2)
    target_compile_options(rescuesched_trace_generator PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(physical_runtime_core PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(rescuesched_physical_runtime PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(physical_runtime_tests PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(wp2_runtime_support_tests PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(wp2_runtime_concurrency_tests PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(rescuesched_handoff_microbench PRIVATE -O2 -Wall -Wextra -Wpedantic)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(rescuesched_rpc_server PRIVATE -O2 -Wall -Wextra -Wpedantic)
        target_compile_options(rescuesched_rpc_client PRIVATE -O2 -Wall -Wextra -Wpedantic)
        target_compile_options(rpc_protocol_tests PRIVATE -O2 -Wall -Wextra -Wpedantic)
    endif()
    if(RESCUESCHED_ENABLE_SANITIZERS)
        set(RESCUESCHED_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_compile_options(physical_runtime_core PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_compile_options(rescuesched_physical_runtime PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_compile_options(physical_runtime_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_compile_options(wp2_runtime_support_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_compile_options(wp2_runtime_concurrency_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_link_options(rescuesched_physical_runtime PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_link_options(physical_runtime_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_link_options(wp2_runtime_support_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        target_link_options(wp2_runtime_concurrency_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(rescuesched_rpc_server PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
            target_compile_options(rescuesched_rpc_client PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
            target_compile_options(rpc_protocol_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
            target_link_options(rescuesched_rpc_server PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
            target_link_options(rescuesched_rpc_client PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
            target_link_options(rpc_protocol_tests PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        endif()
    endif()
    if(RESCUESCHED_ENABLE_THREAD_SANITIZER)
        set(RESCUESCHED_TSAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
        target_compile_options(physical_runtime_core PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_compile_options(rescuesched_physical_runtime PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_compile_options(physical_runtime_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_compile_options(wp2_runtime_support_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_compile_options(wp2_runtime_concurrency_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_link_options(rescuesched_physical_runtime PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_link_options(physical_runtime_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_link_options(wp2_runtime_support_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        target_link_options(wp2_runtime_concurrency_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(rescuesched_rpc_server PRIVATE ${RESCUESCHED_TSAN_FLAGS})
            target_compile_options(rescuesched_rpc_client PRIVATE ${RESCUESCHED_TSAN_FLAGS})
            target_compile_options(rpc_protocol_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
            target_link_options(rescuesched_rpc_server PRIVATE ${RESCUESCHED_TSAN_FLAGS})
            target_link_options(rescuesched_rpc_client PRIVATE ${RESCUESCHED_TSAN_FLAGS})
            target_link_options(rpc_protocol_tests PRIVATE ${RESCUESCHED_TSAN_FLAGS})
        endif()
    endif()
endif()

enable_testing()

add_test(NAME rescue_cli_help
    COMMAND $<TARGET_FILE:simulator> --help
)
set_tests_properties(rescue_cli_help PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Usage:"
)

add_test(NAME rescue_smoke_deterministic
    COMMAND $<TARGET_FILE:simulator> --mode rescue-smoke
)
set_tests_properties(rescue_smoke_deterministic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "RescueSched smoke status: PASS"
)

add_test(NAME simulator_validity
    COMMAND $<TARGET_FILE:simulator_validity_tests>
)

add_test(NAME trace_generator_cli_help
    COMMAND $<TARGET_FILE:rescuesched_trace_generator> --help
)
set_tests_properties(trace_generator_cli_help PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "frozen flow-affine trace"
)
add_test(NAME trace_generate_a
    COMMAND $<TARGET_FILE:rescuesched_trace_generator>
            --out ${CMAKE_CURRENT_BINARY_DIR}/trace-a.csv
            --workload W3 --rho 0.85 --seed 11 --workers 2
            --warmup 20 --requests 100 --flow-count 32
)
add_test(NAME trace_generate_b
    COMMAND $<TARGET_FILE:rescuesched_trace_generator>
            --out ${CMAKE_CURRENT_BINARY_DIR}/trace-b.csv
            --workload W3 --rho 0.85 --seed 11 --workers 2
            --warmup 20 --requests 100 --flow-count 32
)
set_tests_properties(trace_generate_a trace_generate_b PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    FIXTURES_SETUP frozen_trace
)
add_test(NAME trace_reproducible
    COMMAND ${CMAKE_COMMAND} -E compare_files
            ${CMAKE_CURRENT_BINARY_DIR}/trace-a.csv
            ${CMAKE_CURRENT_BINARY_DIR}/trace-b.csv
)
set_tests_properties(trace_reproducible PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    FIXTURES_REQUIRED frozen_trace
)

add_test(NAME physical_runtime_validity
    COMMAND $<TARGET_FILE:physical_runtime_tests>
)
set_tests_properties(physical_runtime_validity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME wp2_runtime_support
    COMMAND $<TARGET_FILE:wp2_runtime_support_tests>
)
add_test(NAME wp2_runtime_concurrency
    COMMAND $<TARGET_FILE:wp2_runtime_concurrency_tests>
)
set_tests_properties(wp2_runtime_support wp2_runtime_concurrency PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME physical_runtime_cli_help
    COMMAND $<TARGET_FILE:rescuesched_physical_runtime> --help
)
set_tests_properties(physical_runtime_cli_help PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local implementation validation"
)
add_test(NAME handoff_microbench_cli_help
    COMMAND $<TARGET_FILE:rescuesched_handoff_microbench> --help
)
set_tests_properties(handoff_microbench_cli_help PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "pinned descriptor handoff microbenchmark"
)
set_tests_properties(simulator_validity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME rpc_server_cli_help
        COMMAND $<TARGET_FILE:rescuesched_rpc_server> --help
    )
    set_tests_properties(rpc_server_cli_help PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION "real UDP request/response server"
    )
    add_test(NAME rpc_client_cli_help
        COMMAND $<TARGET_FILE:rescuesched_rpc_client> --help
    )
    set_tests_properties(rpc_client_cli_help PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION "real open-loop UDP requests"
    )
    add_test(NAME rpc_protocol_validity
        COMMAND $<TARGET_FILE:rpc_protocol_tests>
    )
    set_tests_properties(rpc_protocol_validity PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

add_test(NAME rescue_v2_generate_a
    COMMAND $<TARGET_FILE:simulator>
            --mode rescue-main --workload W3 --rho 0.85 --seed 11
            --warmup-requests 20 --measurement-requests 100
            --output ${CMAKE_CURRENT_BINARY_DIR}/rescue_v2_a.csv
)
add_test(NAME rescue_v2_generate_b
    COMMAND $<TARGET_FILE:simulator>
            --mode rescue-main --workload W3 --rho 0.85 --seed 11
            --warmup-requests 20 --measurement-requests 100
            --output ${CMAKE_CURRENT_BINARY_DIR}/rescue_v2_b.csv
)
set_tests_properties(rescue_v2_generate_a rescue_v2_generate_b PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    FIXTURES_SETUP rescue_v2_csv
)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME rescue_csv_schema
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/validate_rescue_csv_schema.py
                ${CMAKE_CURRENT_BINARY_DIR}/rescue_v2_a.csv
                ${CMAKE_CURRENT_BINARY_DIR}/rescue_v2_b.csv
    )
    set_tests_properties(rescue_csv_schema PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        FIXTURES_REQUIRED rescue_v2_csv
    )

    add_test(NAME cloudlab_order_generate_a
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_cloudlab_run_order.py
                --output ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-a.csv --force
    )
    add_test(NAME cloudlab_order_generate_b
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_cloudlab_run_order.py
                --output ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-b.csv --force
    )
    set_tests_properties(cloudlab_order_generate_a cloudlab_order_generate_b PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        FIXTURES_SETUP cloudlab_order_csv
    )
    add_test(NAME cloudlab_order_reproducible
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-a.csv
                ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-b.csv
    )
    add_test(NAME cloudlab_order_verify
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_cloudlab_run_order.py
                --verify ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-a.csv
    )
    set_tests_properties(cloudlab_order_reproducible cloudlab_order_verify PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        FIXTURES_REQUIRED cloudlab_order_csv
    )

    add_test(NAME wp4_blind_infrastructure_validator
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_wp4_infrastructure_validator.py
                ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME wp4_nonformal_config_identity
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_wp4_nonformal_config.py
                ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(
        wp4_blind_infrastructure_validator
        wp4_nonformal_config_identity
        PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME two_node_coordinator_rejects_commit_mismatch
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_two_node_coordinator_fail_closed.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_test(NAME two_node_coordinator_exact_pid_lifecycle
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_two_node_coordinator_lifecycle.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_test(NAME network_counter_capture_fail_closed
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_network_counter_capture_fail_closed.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_test(NAME background_job_exact_pid_lifecycle
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_background_job_exact_pid.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_test(NAME rpc_affinity_fail_closed
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_rpc_affinity_fail_closed.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
                    $<TARGET_FILE:rescuesched_trace_generator>
                    $<TARGET_FILE:rescuesched_rpc_client>
                    $<TARGET_FILE:rescuesched_rpc_server>
        )
        set_tests_properties(
            two_node_coordinator_rejects_commit_mismatch
            two_node_coordinator_exact_pid_lifecycle
            network_counter_capture_fail_closed
            background_job_exact_pid_lifecycle
            rpc_affinity_fail_closed
            PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()

add_test(NAME rescue_v2_reproducible
    COMMAND ${CMAKE_COMMAND} -E compare_files
            ${CMAKE_CURRENT_BINARY_DIR}/rescue_v2_a.csv
            ${CMAKE_CURRENT_BINARY_DIR}/rescue_v2_b.csv
)
set_tests_properties(rescue_v2_reproducible PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    FIXTURES_REQUIRED rescue_v2_csv
)

add_test(NAME rescue_rejects_zero_check_period
    COMMAND $<TARGET_FILE:simulator>
            --mode rescue-main --workload W3 --rho 0.85 --seed 11
            --warmup-requests 0 --measurement-requests 10
            --check-period-us 0
            --output ${CMAKE_CURRENT_BINARY_DIR}/invalid.csv
)
set_tests_properties(rescue_rejects_zero_check_period PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)

add_test(NAME rescue_rejects_nan_migration_cost
    COMMAND $<TARGET_FILE:simulator>
            --mode rescue-main --workload W3 --rho 0.85 --seed 11
            --warmup-requests 0 --measurement-requests 10
            --migration-cost-us nan
            --output ${CMAKE_CURRENT_BINARY_DIR}/invalid-nan.csv
)
set_tests_properties(rescue_rejects_nan_migration_cost PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
