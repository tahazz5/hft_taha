cmake_minimum_required(VERSION 3.16)

project(orderbook_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(orderbook_tests tests/orderbook_tests.cpp)
add_executable(orderbook_benchmark benchmarks/orderbook_benchmark.cpp)

add_test(NAME orderbook_tests COMMAND orderbook_tests)
