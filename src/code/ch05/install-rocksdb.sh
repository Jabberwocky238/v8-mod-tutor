#!/usr/bin/env bash
set -euo pipefail

: "${LAB_ROOT:?export LAB_ROOT to the multi-repository directory first}"

CMAKE="$LAB_ROOT/.deps/cmake-3.31.8/bin/cmake"
SOURCE="$LAB_ROOT/third_party/rocksdb"
BUILD="$SOURCE/build-v137"
COMMIT=ae8fb3e5000e46d8d4c9dbf3a36019c0aaceebff

if [[ ! -d "$SOURCE/.git" ]]; then
  git clone https://github.com/facebook/rocksdb.git "$SOURCE"
fi
git -C "$SOURCE" fetch --depth=1 origin "$COMMIT"
git -C "$SOURCE" checkout --detach "$COMMIT"

"$CMAKE" -S "$SOURCE" -B "$BUILD" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$LAB_ROOT/v8-mod-tutor/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$LAB_ROOT/.deps/v137" \
  -DROCKSDB_BUILD_SHARED=OFF \
  -DWITH_TESTS=OFF \
  -DWITH_BENCHMARK_TOOLS=OFF \
  -DWITH_CORE_TOOLS=OFF \
  -DWITH_TOOLS=OFF \
  -DWITH_SNAPPY=OFF \
  -DWITH_LZ4=OFF \
  -DWITH_ZLIB=OFF \
  -DWITH_ZSTD=OFF \
  -DWITH_GFLAGS=OFF \
  -DWITH_JEMALLOC=OFF \
  -DPORTABLE=1 \
  -DFAIL_ON_WARNINGS=OFF
"$CMAKE" --build "$BUILD" --target rocksdb
"$CMAKE" --install "$BUILD"

test -f "$LAB_ROOT/.deps/v137/lib/librocksdb.a"
echo "RocksDB 9.10.0:OK"
