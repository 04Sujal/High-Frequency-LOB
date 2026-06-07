cmake_minimum_required(VERSION 3.20)

project(HighFrequencyLOB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lob src/OrderBook.cpp)
target_include_directories(lob PUBLIC include)

if(MSVC)
    target_compile_options(lob PRIVATE /W4)
else()
    target_compile_options(lob PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(lob_demo src/main.cpp)
target_link_libraries(lob_demo PRIVATE lob)

add_executable(lob_tests tests/OrderBookTests.cpp)
target_link_libraries(lob_tests PRIVATE lob)

add_executable(lob_benchmark benchmarks/Benchmark.cpp)
target_link_libraries(lob_benchmark PRIVATE lob)

add_executable(lob_spsc_ingestion examples/SpscIngestion.cpp)
target_link_libraries(lob_spsc_ingestion PRIVATE lob)

enable_testing()
add_test(NAME lob_tests COMMAND lob_tests)
