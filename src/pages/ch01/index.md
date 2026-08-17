---
title: 第 1 章 · 安装 C++、V8 与 KJ 工具链
description: 准备编译 V8 嵌入程序、KJ 网络服务和 cppgc 对象所需的环境
---

<p class="chapter-no">CHAPTER 01 · 安装</p>

# 安装开发环境

<p class="lead">这一章不写运行时代码。目标是得到同一套 Clang 工具链、完整 V8 checkout 和可链接的 KJ 库，避免后面把环境错误误判成生命周期错误。</p>

> **本章新增：构建底座。** V8 提供 JavaScript 引擎和 cppgc；KJ 提供事件循环、Promise、HTTP、WebSocket 与基础容器。二者职责不同，但最终运行在同一个 C++ 进程中。

## 1. 系统依赖

Ubuntu / Debian：

```bash
sudo apt update
sudo apt install -y \
  build-essential clang lld git python3 curl pkg-config \
  cmake ninja-build libssl-dev zlib1g-dev
```

macOS 安装 Xcode Command Line Tools、CMake 和 Ninja。建议至少 8 GB 内存、25 GB 可用磁盘；第一次构建 V8 是主要开销。

## 2. depot_tools 与 V8 依赖

```bash
mkdir -p "$HOME/tools"
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
  "$HOME/tools/depot_tools"
export PATH="$HOME/tools/depot_tools:$PATH"
export LAB_ROOT=/home/zq/v8v8

cd "$LAB_ROOT"
gclient config --unmanaged https://chromium.googlesource.com/v8/v8.git
gclient sync
```

如果已有 `$LAB_ROOT/.gclient`，跳过 `gclient config`。同步成功后，`v8/buildtools`、`v8/third_party/icu` 和 V8 自带 Clang 工具链都应存在。

## 3. 生成带 cppgc 的 V8 构建

```bash
cd "$LAB_ROOT/v8"
tools/dev/v8gen.py x64.release -- \
  is_component_build=false \
  v8_monolithic=true \
  v8_use_external_startup_data=false \
  v8_enable_i18n_support=true \
  v8_enable_pointer_compression=true
autoninja -C out.gn/x64.release v8_monolith
```

`v8_monolith` 方便教程宿主链接；正式产品可以拆分目标。cppgc 是 V8 源码的一部分，不需要另装垃圾回收库。

## 4. 获取 KJ

KJ 随 Cap'n Proto 发布。教程建议固定版本，不跟随系统随机升级：

```bash
cd "$LAB_ROOT"
git clone --depth 1 --branch v1.1.0 \
  https://github.com/capnproto/capnproto.git third_party/capnproto
cmake -S third_party/capnproto -B third_party/capnproto/build \
  -GNinja -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
cmake --build third_party/capnproto/build --target kj kj-test kj-async kj-http
```

> **KJ 事件循环。** `kj-async` 提供 `kj::Promise`、`kj::Timer` 和网络事件端口；`kj-http` 在其上实现 HTTP/1.1 与 WebSocket。后续不会额外引入第二套 event loop。

## 5. 目录约定

```text
v8-mod-tutor/runtime/
├── CMakeLists.txt
├── src/             C++ 宿主
├── worker/          被 V8 加载的 JavaScript
├── public/          第 4 章 SPA
├── data/            第 4、5 章日志与 RocksDB
└── test/
```

## 6. 验收

```bash
test -f "$LAB_ROOT/v8/out.gn/x64.release/obj/libv8_monolith.a" && echo V8:OK
test -f "$LAB_ROOT/third_party/capnproto/build/src/kj/libkj-async.a" && echo KJ:OK
clang++ --version
ninja --version
```

全部通过再进入 Hello World。构建失败时先保留完整日志；不要靠重复安装系统库碰运气。

<nav class="pager"><a href="../">← 课程首页</a><a href="../ch02/">第 2 章：Hello World →</a></nav>
