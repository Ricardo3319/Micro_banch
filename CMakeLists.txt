cmake_minimum_required(VERSION 3.16)
project(micro_migration_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sim_core
    src/core/simulator.cpp
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

add_library(qbr_core src/qbr/simulator.cpp)
target_include_directories(qbr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(batchsched src/qbr/main.cpp)
target_link_libraries(batchsched PRIVATE qbr_core)
include(CTest)
if(BUILD_TESTING)
    add_executable(qbr_tests tests/qbr_tests.cpp)
    target_link_libraries(qbr_tests PRIVATE qbr_core)
    add_test(NAME qbr_invariants COMMAND qbr_tests)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qbr_core PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(simulator PRIVATE -O2)
endif()
