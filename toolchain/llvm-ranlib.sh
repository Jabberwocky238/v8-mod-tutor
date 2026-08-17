#!/bin/sh
set -eu
: "${LAB_ROOT:?LAB_ROOT must point to the multi-repository root}"
exec "${LAB_ROOT}/.deps/chromium-clang-21/bin/llvm-ar" s "$@"
