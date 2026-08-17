cmake_minimum_required(VERSION 3.20)
project(v8lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED ENV{LAB_ROOT})
  message(FATAL_ERROR "LAB_ROOT must point to the multi-repository root")
endif()

set(LAB_ROOT "$ENV{LAB_ROOT}")
set(V8_ROOT "${LAB_ROOT}/third_party/v8-13.7.152.14")
set(V8_ARCHIVE "${LAB_ROOT}/.deps/v8-13.7.152.14/libv8_cpp.a")

if(NOT EXISTS "${V8_ARCHIVE}")
  message(FATAL_ERROR "Pinned V8 archive not found; finish Chapter 1 first")
endif()

find_package(Threads REQUIRED)
find_package(CapnProto CONFIG REQUIRED)

add_library(v8_monolith STATIC IMPORTED)
set_target_properties(v8_monolith PROPERTIES
  IMPORTED_LOCATION "${V8_ARCHIVE}")

add_library(v8lab_engine src/engine.c++)
target_include_directories(v8lab_engine PUBLIC src "${V8_ROOT}/include")
target_compile_definitions(v8lab_engine PUBLIC V8_COMPRESS_POINTERS)
target_link_libraries(v8lab_engine PUBLIC
  v8_monolith CapnProto::kj Threads::Threads ${CMAKE_DL_LIBS})

add_executable(v8lab src/main.c++)
target_link_libraries(v8lab PRIVATE v8lab_engine)

enable_testing()
add_executable(engine-test test/engine-test.c++)
target_link_libraries(engine-test PRIVATE v8lab_engine CapnProto::kj-test)
add_test(NAME engine COMMAND engine-test)
