cmake_minimum_required(VERSION 3.16)
project(rtos_data_structures LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(data_structures
    ds/src/bitmap.cpp
    ds/src/timing_wheel.cpp
    ds/src/DynamicArray.cpp
)
target_include_directories(data_structures PUBLIC ds/include)

add_executable(test_ds
    tests/test_ds.cpp
    tests/test_bitmap.cpp
    tests/test_circular_list.cpp
    tests/test_hash_map.cpp
    tests/test_timing_wheel.cpp
)
target_link_libraries(test_ds PRIVATE data_structures)

enable_testing()
add_test(NAME data_structures COMMAND test_ds)

# MinHeap is header-only; linking data_structures provides ds/include.
add_executable(test_minheap tests/test_minheap.cpp)
target_link_libraries(test_minheap PRIVATE data_structures)
add_test(NAME minheap COMMAND test_minheap)

# sortedList is header-only (its header includes ds/src/sortedList.tpp).
add_executable(test_sorted_list tests/test_sortedList.cpp)
target_link_libraries(test_sorted_list PRIVATE data_structures)
add_test(NAME sorted_list COMMAND test_sorted_list)

# DynamicArray is compiled into data_structures; its suite has its own main.
add_executable(test_dynamic_array tests/test_dynamic_array.cpp)
target_link_libraries(test_dynamic_array PRIVATE data_structures)
add_test(NAME dynamic_array COMMAND test_dynamic_array)

# Shared records and registry; scheduler/resource behavior is integrated later.
add_library(kernel_foundation
    kernel/src/registry.cpp
    kernel/src/validation.cpp
    kernel/src/rate_monotonic.cpp
)
target_include_directories(kernel_foundation PUBLIC kernel/include)
target_link_libraries(kernel_foundation PUBLIC data_structures)

add_executable(test_kernel_foundation tests/test_kernel_foundation.cpp)
target_link_libraries(test_kernel_foundation PRIVATE kernel_foundation)
add_test(NAME kernel_foundation COMMAND test_kernel_foundation)

add_library(kernel_resources kernel/src/resource_manager.cpp)
target_link_libraries(kernel_resources PUBLIC kernel_foundation)

add_executable(test_resource_manager tests/test_resource_manager.cpp)
target_link_libraries(test_resource_manager PRIVATE kernel_resources)
add_test(NAME resource_manager COMMAND test_resource_manager)

# Timer manager: sortedList and MinHeap backends behind one contract.
add_library(kernel_timers kernel/src/timer_manager.cpp)
target_link_libraries(kernel_timers PUBLIC kernel_foundation)

add_executable(test_timer_manager tests/test_timer_manager.cpp)
target_link_libraries(test_timer_manager PRIVATE kernel_timers)
add_test(NAME timer_manager COMMAND test_timer_manager)

# Bounded run trace (events, CPU segments, snapshots) filled by the engine.
add_library(kernel_trace kernel/src/run_trace.cpp)
target_link_libraries(kernel_trace PUBLIC kernel_resources)

# JSON run-file exporter (docs/run-schema-v1.md). The build's git commit is
# written as run.engineRevision so every exported run names its code version.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE B6_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
add_library(run_export app/src/run_exporter.cpp)
target_include_directories(run_export PUBLIC app/include)
target_link_libraries(run_export PUBLIC kernel_trace)
target_compile_definitions(run_export PRIVATE B6_ENGINE_REVISION="${B6_GIT_REVISION}")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/export_check)
add_executable(test_run_export tests/test_run_export.cpp)
target_link_libraries(test_run_export PRIVATE run_export kernel_timers)
add_test(NAME run_export
         COMMAND test_run_export ${CMAKE_CURRENT_BINARY_DIR}/export_check)

# Cross-check: the exported files must pass the dashboard's own validator.
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME run_export_dashboard_validation
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_export.js
                     ${CMAKE_CURRENT_BINARY_DIR}/export_check)
    set_tests_properties(run_export_dashboard_validation PROPERTIES DEPENDS run_export)
endif()

# Scheduler (Youssef Gohar): clock, ready rings + bitmap, EDF heap, dispatch,
# script interpreter and suspend/resume. Calls resources, timers and trace.
add_library(kernel_scheduler kernel/src/scheduler.cpp)
target_link_libraries(kernel_scheduler PUBLIC kernel_resources kernel_timers kernel_trace)

# Application layer: one-run Simulation wrapper and the named preset scenarios.
add_library(b6_app app/src/simulation.cpp app/src/presets.cpp app/src/metrics.cpp)
target_include_directories(b6_app PUBLIC app/include)
target_link_libraries(b6_app PUBLIC kernel_scheduler run_export)

# The command-line simulator: preset -> scheduler -> run file for the dashboard.
add_executable(b6_sim app/src/main.cpp)
target_link_libraries(b6_sim PRIVATE b6_app)

# Golden traces, invariants, both timer backends, repeatability and errors.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/scheduler_runs)
add_executable(test_scheduler tests/test_scheduler.cpp)
target_link_libraries(test_scheduler PRIVATE b6_app)
add_test(NAME scheduler
         COMMAND test_scheduler ${CMAKE_CURRENT_BINARY_DIR}/scheduler_runs)

# Metrics: response, misses, jitter, utilization and RM bound applicability.
add_executable(test_metrics tests/test_metrics.cpp)
target_link_libraries(test_metrics PRIVATE b6_app)
add_test(NAME metrics COMMAND test_metrics)

# Result tables (docs/results.md): golden-trace comparison and timer costs.
# Not CTests: b6_timer_cost timings vary by machine. Build Release to time.
add_executable(b6_golden experiments/golden_compare.cpp)
target_link_libraries(b6_golden PRIVATE b6_app)
add_executable(b6_timer_cost experiments/timer_cost.cpp)
target_link_libraries(b6_timer_cost PRIVATE kernel_timers)

# Every real engine run must also pass the dashboard's own import validator.
if(NODE_EXECUTABLE)
    add_test(NAME scheduler_runs_dashboard_validation
             COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_runs.js
                     ${CMAKE_CURRENT_BINARY_DIR}/scheduler_runs)
    set_tests_properties(scheduler_runs_dashboard_validation PROPERTIES DEPENDS scheduler)
endif()
