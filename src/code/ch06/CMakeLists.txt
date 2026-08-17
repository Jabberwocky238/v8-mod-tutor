cmake_minimum_required(VERSION 3.28)
project(v8lab_ch06 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED ENV{LAB_ROOT})
  message(FATAL_ERROR "LAB_ROOT must point to the multi-repository root")
endif()

find_package(Threads REQUIRED)
find_package(CapnProto CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)

set(CH05 "${CMAKE_CURRENT_SOURCE_DIR}/../ch05/src")
add_library(observability
  src/trace.c++
  src/write-coordinator.c++
  ${CH05}/kv-store.c++)
target_include_directories(observability PUBLIC src "${CH05}")
target_link_libraries(observability PUBLIC
  RocksDB::rocksdb CapnProto::kj Threads::Threads ${CMAKE_DL_LIBS})

add_executable(trace-test test/trace-test.c++)
target_link_libraries(trace-test PRIVATE observability CapnProto::kj-test)

add_executable(write-benchmark src/write-benchmark.c++)
target_link_libraries(write-benchmark PRIVATE observability)

enable_testing()
add_test(NAME trace COMMAND trace-test)
