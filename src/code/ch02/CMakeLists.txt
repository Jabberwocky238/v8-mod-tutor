cmake_minimum_required(VERSION 3.20)
project(v8lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED ENV{LAB_ROOT})
  message(FATAL_ERROR "LAB_ROOT must point to the multi-repository root")
endif()

set(LAB_ROOT "$ENV{LAB_ROOT}")
set(V8_ROOT "${LAB_ROOT}/v8")
set(V8_OUT "${V8_ROOT}/out.gn/x64.release")
set(KJ_ROOT "${LAB_ROOT}/third_party/capnproto")
set(KJ_OUT "${KJ_ROOT}/build/src/kj")

find_package(Threads REQUIRED)

add_library(v8_monolith STATIC IMPORTED)
set_target_properties(v8_monolith PROPERTIES
  IMPORTED_LOCATION "${V8_OUT}/obj/libv8_monolith.a")

add_library(kj STATIC IMPORTED)
set_target_properties(kj PROPERTIES IMPORTED_LOCATION "${KJ_OUT}/libkj.a")

add_library(kj_test STATIC IMPORTED)
set_target_properties(kj_test PROPERTIES
  IMPORTED_LOCATION "${KJ_OUT}/libkj-test.a")

add_library(v8lab_engine src/engine.c++)
target_include_directories(v8lab_engine PUBLIC src "${V8_ROOT}/include")
target_compile_definitions(v8lab_engine PUBLIC
  V8_COMPRESS_POINTERS V8_ENABLE_SANDBOX)
target_link_libraries(v8lab_engine PUBLIC
  v8_monolith kj Threads::Threads ${CMAKE_DL_LIBS})

add_executable(v8lab src/main.c++)
target_link_libraries(v8lab PRIVATE v8lab_engine)

enable_testing()
add_executable(engine-test test/engine-test.c++)
target_include_directories(engine-test PRIVATE "${KJ_ROOT}/c++/src")
target_link_libraries(engine-test PRIVATE v8lab_engine kj_test)
add_test(NAME engine COMMAND engine-test)
