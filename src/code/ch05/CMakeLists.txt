cmake_minimum_required(VERSION 3.28)
project(v8lab_ch05 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED ENV{LAB_ROOT})
  message(FATAL_ERROR "LAB_ROOT must point to the multi-repository root")
endif()

set(LAB_ROOT "$ENV{LAB_ROOT}")
set(V8_ROOT "${LAB_ROOT}/third_party/v8-13.7.152.14")
set(V8_ARCHIVE "${LAB_ROOT}/.deps/v8-13.7.152.14/libv8_cpp.a")
set(CH03 "${CMAKE_CURRENT_SOURCE_DIR}/../ch03/src")

find_package(Threads REQUIRED)
find_package(CapnProto CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)

add_library(v8_monolith STATIC IMPORTED)
set_target_properties(v8_monolith PROPERTIES IMPORTED_LOCATION "${V8_ARCHIVE}")

add_library(storage_runtime
  ${CH03}/execution-context.c++
  ${CH03}/runtime.c++
  ${CH03}/timer-queue.c++
  src/kv-binding.c++
  src/kv-store.c++
  src/storage-executor.c++)
target_include_directories(storage_runtime PUBLIC src "${CH03}" "${V8_ROOT}/include")
target_compile_definitions(storage_runtime PUBLIC V8_COMPRESS_POINTERS)
target_link_libraries(storage_runtime PUBLIC
  v8_monolith RocksDB::rocksdb CapnProto::kj-async Threads::Threads ${CMAKE_DL_LIBS})

add_executable(v8-kv src/main.c++)
target_link_libraries(v8-kv PRIVATE storage_runtime)

enable_testing()
add_executable(storage-test test/storage-test.c++)
target_link_libraries(storage-test PRIVATE storage_runtime CapnProto::kj-test)
add_test(NAME storage COMMAND storage-test)
