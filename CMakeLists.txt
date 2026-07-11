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

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(simulator PRIVATE -O2)
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

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME rescue_csv_schema
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/validate_rescue_csv_schema.py
                ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/step-15-rescuesched/rescue_main.csv
                ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/step-18-infocom-readiness/rescue_estimator_main.csv
    )
    set_tests_properties(rescue_csv_schema PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
