cmake_minimum_required(VERSION 3.16)
project(rtos_data_structures LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(data_structures
    ds/src/bitmap.cpp
    ds/src/timing_wheel.cpp
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

# Shared records and registry; scheduler/resource behavior is integrated later.
add_library(kernel_foundation
    kernel/src/registry.cpp
    kernel/src/validation.cpp
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
