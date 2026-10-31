cmake_minimum_required(VERSION 3.16)
project(rtos_data_structures LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(data_structures
    ds/src/bitmap.cpp
)
target_include_directories(data_structures PUBLIC ds/include)

add_executable(test_ds
    tests/test_ds.cpp
    tests/test_bitmap.cpp
    tests/test_circular_list.cpp
    tests/test_hash_map.cpp
)
target_link_libraries(test_ds PRIVATE data_structures)

enable_testing()
add_test(NAME data_structures COMMAND test_ds)
