cmake_minimum_required(VERSION 3.28)
project(v8lab_ch04 LANGUAGES CXX)

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

add_library(v8_monolith STATIC IMPORTED)
set_target_properties(v8_monolith PROPERTIES IMPORTED_LOCATION "${V8_ARCHIVE}")

add_library(chat_runtime
  ${CH03}/execution-context.c++
  ${CH03}/fetch-service.c++
  ${CH03}/runtime.c++
  ${CH03}/timer-queue.c++
  src/append-log.c++
  src/chat-hub.c++
  src/router.c++
  src/router-service.c++
  src/websocket-session.c++)
target_include_directories(chat_runtime PUBLIC src "${CH03}" "${V8_ROOT}/include")
target_compile_definitions(chat_runtime PUBLIC V8_COMPRESS_POINTERS)
target_link_libraries(chat_runtime PUBLIC
  v8_monolith CapnProto::kj-http Threads::Threads ${CMAKE_DL_LIBS})

add_executable(v8-chat src/main.c++)
target_link_libraries(v8-chat PRIVATE chat_runtime)

enable_testing()
add_executable(chatroom-test test/chatroom-test.c++)
target_link_libraries(chatroom-test PRIVATE chat_runtime CapnProto::kj-test)
add_test(NAME chatroom COMMAND chatroom-test)
