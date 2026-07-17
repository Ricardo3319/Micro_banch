cmake_minimum_required(VERSION 3.16)
project(rescuesched_physical LANGUAGES CXX)

option(RESCUESCHED_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RESCUESCHED_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(RESCUESCHED_ENABLE_SANITIZERS AND RESCUESCHED_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(trace_core src/core/workload_trace.cpp)
target_include_directories(trace_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(physical_runtime_core
    src/physical/runtime.cpp
    src/physical/trace.cpp
)
target_include_directories(physical_runtime_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(physical_runtime_core PUBLIC trace_core Threads::Threads)

add_executable(rescuesched_trace_generator src/app/trace_generator_main.cpp)
target_link_libraries(rescuesched_trace_generator PRIVATE trace_core)

add_executable(rescuesched_physical_runtime src/app/physical_runtime_main.cpp)
target_link_libraries(rescuesched_physical_runtime PRIVATE physical_runtime_core)

add_executable(rescuesched_handoff_microbench src/app/handoff_microbench_main.cpp)
target_link_libraries(rescuesched_handoff_microbench PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(rescuesched_rpc_server src/app/rpc_server_main.cpp)
    target_link_libraries(rescuesched_rpc_server PRIVATE physical_runtime_core)

    add_executable(rescuesched_rpc_client src/app/rpc_client_main.cpp)
    target_link_libraries(rescuesched_rpc_client PRIVATE physical_runtime_core)
endif()

add_executable(physical_runtime_tests tests/unit/physical_runtime_tests.cpp)
target_link_libraries(physical_runtime_tests PRIVATE physical_runtime_core)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(rpc_protocol_tests tests/unit/rpc_protocol_tests.cpp)
    target_link_libraries(rpc_protocol_tests PRIVATE physical_runtime_core)
endif()

set(RESCUESCHED_WARNING_FLAGS -O2 -Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    foreach(target
            trace_core physical_runtime_core rescuesched_trace_generator
            rescuesched_physical_runtime rescuesched_handoff_microbench
            physical_runtime_tests)
        target_compile_options(${target} PRIVATE ${RESCUESCHED_WARNING_FLAGS})
    endforeach()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        foreach(target rescuesched_rpc_server rescuesched_rpc_client rpc_protocol_tests)
            target_compile_options(${target} PRIVATE ${RESCUESCHED_WARNING_FLAGS})
        endforeach()
    endif()

    if(RESCUESCHED_ENABLE_SANITIZERS)
        set(RESCUESCHED_SANITIZER_FLAGS
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    elseif(RESCUESCHED_ENABLE_THREAD_SANITIZER)
        set(RESCUESCHED_SANITIZER_FLAGS
            -fsanitize=thread -fno-omit-frame-pointer)
    endif()

    if(RESCUESCHED_SANITIZER_FLAGS)
        foreach(target physical_runtime_core rescuesched_physical_runtime
                       physical_runtime_tests)
            target_compile_options(${target} PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        endforeach()
        foreach(target rescuesched_physical_runtime physical_runtime_tests)
            target_link_options(${target} PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
        endforeach()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            foreach(target rescuesched_rpc_server rescuesched_rpc_client)
                target_compile_options(${target} PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
                target_link_options(${target} PRIVATE ${RESCUESCHED_SANITIZER_FLAGS})
            endforeach()
        endif()
    endif()
endif()

enable_testing()

add_test(NAME trace_generator_cli_help
    COMMAND $<TARGET_FILE:rescuesched_trace_generator> --help)
set_tests_properties(trace_generator_cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "frozen flow-affine trace")

add_test(NAME trace_generate_a
    COMMAND $<TARGET_FILE:rescuesched_trace_generator>
            --out ${CMAKE_CURRENT_BINARY_DIR}/trace-a.csv
            --workload W3 --rho 0.85 --seed 11 --workers 2
            --warmup 20 --requests 100 --flow-count 32)
add_test(NAME trace_generate_b
    COMMAND $<TARGET_FILE:rescuesched_trace_generator>
            --out ${CMAKE_CURRENT_BINARY_DIR}/trace-b.csv
            --workload W3 --rho 0.85 --seed 11 --workers 2
            --warmup 20 --requests 100 --flow-count 32)
set_tests_properties(trace_generate_a trace_generate_b PROPERTIES
    FIXTURES_SETUP frozen_trace)
add_test(NAME trace_reproducible
    COMMAND ${CMAKE_COMMAND} -E compare_files
            ${CMAKE_CURRENT_BINARY_DIR}/trace-a.csv
            ${CMAKE_CURRENT_BINARY_DIR}/trace-b.csv)
set_tests_properties(trace_reproducible PROPERTIES FIXTURES_REQUIRED frozen_trace)

add_test(NAME physical_runtime_validity COMMAND $<TARGET_FILE:physical_runtime_tests>)
add_test(NAME physical_runtime_cli_help
    COMMAND $<TARGET_FILE:rescuesched_physical_runtime> --help)
set_tests_properties(physical_runtime_cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "local implementation validation")

add_test(NAME handoff_microbench_cli_help
    COMMAND $<TARGET_FILE:rescuesched_handoff_microbench> --help)
set_tests_properties(handoff_microbench_cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "pinned descriptor handoff microbenchmark")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME rpc_server_cli_help COMMAND $<TARGET_FILE:rescuesched_rpc_server> --help)
    set_tests_properties(rpc_server_cli_help PROPERTIES
        PASS_REGULAR_EXPRESSION "real UDP request/response server")
    add_test(NAME rpc_client_cli_help COMMAND $<TARGET_FILE:rescuesched_rpc_client> --help)
    set_tests_properties(rpc_client_cli_help PROPERTIES
        PASS_REGULAR_EXPRESSION "real open-loop UDP requests")
    add_test(NAME rpc_protocol_validity COMMAND $<TARGET_FILE:rpc_protocol_tests>)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME cloudlab_order_generate_a
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_cloudlab_run_order.py
                --output ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-a.csv --force)
    add_test(NAME cloudlab_order_generate_b
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_cloudlab_run_order.py
                --output ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-b.csv --force)
    set_tests_properties(cloudlab_order_generate_a cloudlab_order_generate_b PROPERTIES
        FIXTURES_SETUP cloudlab_order)
    add_test(NAME cloudlab_order_reproducible
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-a.csv
                ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-b.csv)
    set_tests_properties(cloudlab_order_reproducible PROPERTIES
        FIXTURES_REQUIRED cloudlab_order)
    add_test(NAME cloudlab_order_verify
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_cloudlab_run_order.py
                --verify ${CMAKE_CURRENT_BINARY_DIR}/cloudlab-order-a.csv)
    set_tests_properties(cloudlab_order_verify PROPERTIES
        FIXTURES_REQUIRED cloudlab_order)
endif()
