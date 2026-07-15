cmake_minimum_required(VERSION 3.16)
project(micro_migration_sim LANGUAGES CXX)

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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(simulator PRIVATE -O2)
    target_compile_options(simulator_validity_tests PRIVATE -O2)
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
set_tests_properties(simulator_validity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

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
