cmake_minimum_required(VERSION 3.28)
project(v8lab_ch03 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED ENV{LAB_ROOT})
  message(FATAL_ERROR "LAB_ROOT must point to the multi-repository root")
endif()

set(LAB_ROOT "$ENV{LAB_ROOT}")
set(V8_ROOT "${LAB_ROOT}/third_party/v8-13.7.152.14")
set(V8_ARCHIVE "${LAB_ROOT}/.deps/v8-13.7.152.14/libv8_cpp.a")

find_package(Threads REQUIRED)
find_package(CapnProto CONFIG REQUIRED)

add_library(v8_monolith STATIC IMPORTED)
set_target_properties(v8_monolith PROPERTIES IMPORTED_LOCATION "${V8_ARCHIVE}")

add_library(fetch_runtime
  src/execution-context.c++
  src/fetch-service.c++
  src/runtime.c++
  src/timer-queue.c++)
target_include_directories(fetch_runtime PUBLIC src "${V8_ROOT}/include")
target_compile_definitions(fetch_runtime PUBLIC V8_COMPRESS_POINTERS)
target_link_libraries(fetch_runtime PUBLIC
  v8_monolith CapnProto::kj-http Threads::Threads ${CMAKE_DL_LIBS})

add_executable(v8-fetch src/main.c++)
target_link_libraries(v8-fetch PRIVATE fetch_runtime)

enable_testing()
add_executable(fetch-runtime-test test/fetch-runtime-test.c++)
target_link_libraries(fetch-runtime-test PRIVATE fetch_runtime CapnProto::kj-test)
add_test(NAME fetch-runtime COMMAND fetch-runtime-test)
