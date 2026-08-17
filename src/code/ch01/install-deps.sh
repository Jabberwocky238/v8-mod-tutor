#!/usr/bin/env bash
set -euo pipefail

: "${LAB_ROOT:?export LAB_ROOT to the multi-repository directory first}"

DEPS="$LAB_ROOT/.deps"
THIRD_PARTY="$LAB_ROOT/third_party"
TUTOR="$LAB_ROOT/v8-mod-tutor"
DOWNLOADS="$DEPS/downloads"

CMAKE_VERSION=3.31.8
NINJA_VERSION=1.12.1
V8_VERSION=13.7.152.14
V8_ARCHIVE_URL=https://github.com/denoland/rusty_v8/releases/download/v137.3.0/librusty_v8_ptrcomp_release_x86_64-unknown-linux-gnu.a.gz
CLANG_URL=https://commondatastorage.googleapis.com/chromium-browser-clang/Linux_x64/clang-llvmorg-21-init-9266-g09006611-1.tar.xz

mkdir -p "$DEPS" "$THIRD_PARTY" "$DOWNLOADS"

download() {
  local url="$1" output="$2" sha256="$3"
  if [[ ! -f "$output" ]]; then
    curl --fail --location --retry 3 "$url" --output "$output"
  fi
  echo "$sha256  $output" | sha256sum --check --status
}

checkout() {
  local url="$1" directory="$2" commit="$3" sparse="$4"
  if [[ ! -d "$directory/.git" ]]; then
    git clone --filter=blob:none --no-checkout "$url" "$directory"
  fi
  git -C "$directory" sparse-checkout set $sparse
  git -C "$directory" fetch --depth=1 origin "$commit"
  git -C "$directory" checkout --detach FETCH_HEAD
}

download \
  "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz" \
  "$DOWNLOADS/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz" \
  630615d8e98ac33eba7fbe472626dff5c899c85af3c024585ae109166a6909d0
mkdir -p "$DEPS/cmake-$CMAKE_VERSION"
tar -xzf "$DOWNLOADS/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz" \
  -C "$DEPS/cmake-$CMAKE_VERSION" --strip-components=1
CMAKE="$DEPS/cmake-$CMAKE_VERSION/bin/cmake"

download \
  "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/ninja-linux.zip" \
  "$DOWNLOADS/ninja-linux-$NINJA_VERSION.zip" \
  6f98805688d19672bd699fbbfa2c2cf0fc054ac3df1f0e6a47664d963d530255
mkdir -p "$DEPS/ninja-$NINJA_VERSION"
cd "$DEPS/ninja-$NINJA_VERSION"
"$CMAKE" -E tar xf "$DOWNLOADS/ninja-linux-$NINJA_VERSION.zip"
chmod +x ninja

download "$CLANG_URL" "$DOWNLOADS/chromium-clang-21.tar.xz" \
  2cccd3a5b04461f17a2e78d2f8bd18b448443a9dd4d6dfac50e8e84b4d5176f1
mkdir -p "$DEPS/chromium-clang-21"
tar -xJf "$DOWNLOADS/chromium-clang-21.tar.xz" \
  -C "$DEPS/chromium-clang-21"

download "$V8_ARCHIVE_URL" "$DOWNLOADS/v8-$V8_VERSION.a.gz" \
  74ff36bf54cb61dd69a25372e4e67d566f571e3cdf69145fbea1c664014cc6e6
mkdir -p "$DEPS/v8-$V8_VERSION"
gzip -dc "$DOWNLOADS/v8-$V8_VERSION.a.gz" > "$DEPS/v8-$V8_VERSION/libv8_cpp.a"

LLVM_NM="$DEPS/chromium-clang-21/bin/llvm-nm"
if "$LLVM_NM" -u "$DEPS/v8-$V8_VERSION/libv8_cpp.a" | grep -q 'temporal_rs_'; then
  echo "error: archive requires a Rust Temporal ABI" >&2
  exit 1
fi
if "$LLVM_NM" --defined-only "$DEPS/v8-$V8_VERSION/libv8_cpp.a" \
    | grep -Eq ' [TW] (malloc|free)$'; then
  echo "error: archive overrides the process allocator" >&2
  exit 1
fi

checkout https://github.com/denoland/v8.git \
  "$THIRD_PARTY/v8-$V8_VERSION" \
  f68bbb6cda689a14d019a6a60cc93963724e7c35 include
checkout https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxx.git \
  "$THIRD_PARTY/libcxx-v137" \
  917609c669e43edc850eeb192a342434a54e1dfd include
checkout https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxxabi.git \
  "$THIRD_PARTY/libcxxabi-v137" \
  f2a7f2987f9dcdf8b04c2d8cd4dcb186641a7c3e include
checkout https://chromium.googlesource.com/chromium/src/buildtools.git \
  "$THIRD_PARTY/buildtools-v137" \
  0f32cb9025766951122d4ed19aba87a94ded3f43 third_party/libc++
checkout https://github.com/capnproto/capnproto.git \
  "$THIRD_PARTY/capnproto" \
  a1cd1c4b3d241b77478035a6ccad8b0fb587d444 c++ CMakeLists.txt cmake

export LAB_ROOT
"$CMAKE" -S "$THIRD_PARTY/capnproto" -B "$THIRD_PARTY/capnproto/build-v137" \
  -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$TUTOR/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="$DEPS/v137" \
  -DBUILD_TESTING=ON \
  -DWITH_OPENSSL=OFF \
  -DWITH_ZLIB=OFF
"$CMAKE" --build "$THIRD_PARTY/capnproto/build-v137"
"$DEPS/cmake-$CMAKE_VERSION/bin/ctest" \
  --test-dir "$THIRD_PARTY/capnproto/build-v137" --output-on-failure
"$CMAKE" --install "$THIRD_PARTY/capnproto/build-v137"

echo "V8_CPP:OK"
echo "KJ:OK"
"$DEPS/chromium-clang-21/bin/clang++" --version | head -n 1
"$DEPS/ninja-$NINJA_VERSION/ninja" --version
