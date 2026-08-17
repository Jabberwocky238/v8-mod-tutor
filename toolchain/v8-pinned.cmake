if(NOT DEFINED ENV{LAB_ROOT})
  message(FATAL_ERROR "LAB_ROOT must point to the multi-repository root")
endif()

set(LAB_ROOT "$ENV{LAB_ROOT}")
set(V8_CLANG "${LAB_ROOT}/.deps/chromium-clang-21/bin")
set(V8_PREBUILT_ROOT "${LAB_ROOT}/.deps/v8-13.7.152.14")
set(V8_PREBUILT_ARCHIVE "${V8_PREBUILT_ROOT}/libv8_cpp.a")
set(V8_HEADERS "${LAB_ROOT}/third_party/v8-13.7.152.14/include")
set(LIBCXX_HEADERS "${LAB_ROOT}/third_party/libcxx-v137/include")
set(LIBCXXABI_HEADERS "${LAB_ROOT}/third_party/libcxxabi-v137/include")
set(LIBCXX_CONFIG
  "${LAB_ROOT}/third_party/buildtools-v137/third_party/libc++")

foreach(required
    "${V8_CLANG}/clang"
    "${V8_CLANG}/clang++"
    "${V8_PREBUILT_ARCHIVE}"
    "${V8_HEADERS}/v8.h"
    "${LIBCXX_CONFIG}/__config_site"
    "${LIBCXX_CONFIG}/__assertion_handler"
    "${LIBCXX_HEADERS}/__config"
    "${LIBCXXABI_HEADERS}/cxxabi.h")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Pinned V8 toolchain file is missing: ${required}")
  endif()
endforeach()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER "${V8_CLANG}/clang" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${V8_CLANG}/clang++" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${V8_CLANG}/llvm-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_MAKE_PROGRAM "${LAB_ROOT}/.deps/ninja-1.12.1/ninja"
  CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${LAB_ROOT}/v8-mod-tutor/toolchain/llvm-ranlib.sh"
  CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "" FORCE)

set(PINNED_CXX_INCLUDES
  "-nostdinc++ -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE -isystem${LIBCXX_CONFIG} -isystem${LIBCXX_HEADERS} -isystem${LIBCXXABI_HEADERS}")
set(CMAKE_CXX_FLAGS_INIT "${PINNED_CXX_INCLUDES}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib++")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT
  "${V8_PREBUILT_ARCHIVE} -lpthread -ldl -lm")

# Never search the host for tutorial dependencies.
set(CMAKE_FIND_ROOT_PATH "${LAB_ROOT}/.deps")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
