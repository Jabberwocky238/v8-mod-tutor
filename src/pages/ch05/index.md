---
title: 第 5 章 · 用 RocksDB 实现 env.KV
description: 用锁定工具链构建 RocksDB，并实现异步 get、put、delete 与 list
---

<p class="chapter-no">CHAPTER 05 · Storage</p>

# 接入 RocksDB，做一个 KV 数据库

<p class="lead">第四章的房间和消息仍然只在内存。本章加入 RocksDB，并向 JavaScript 提供 <code>env.KV</code>。数据库调用在工作线程执行，V8 handle 永远留在 Runtime 线程。</p>

> **本章新增：`KvStore`。** 它唯一拥有 RocksDB DB，负责物理 key、大小限制、CRUD、前缀 list 和错误转换。这个类保持同步，因为 RocksDB API 本身就是同步的。

> **本章新增：`StorageExecutor`。** 固定线程在有界队列中执行阻塞操作，使用 KJ cross-thread fulfiller 把结果送回创建 Promise 的 event loop。

> **本章新增：`KvBinding`。** 它把 `get()`、`put()`、`delete()`、`list()` 安装到 `env.KV`，并且只在 V8 线程创建、resolve 或 reject JavaScript Promise。

## 1. 用同一工具链构建 RocksDB

RocksDB `v9.10.0` 的官方 Release 没有 Linux 静态库资产；系统包又使用 libstdc++，不能与本教程的 Chromium libc++ ABI 混用。因此固定 commit `ae8fb3e…`，自行构建一次静态库。

压缩库、jemalloc、gflags、benchmark 和命令行工具都不是 KV 教学所需，全部关闭。这既避免系统动态依赖，也缩小需要理解的依赖面。

<details class="code-accordion" data-code="ch05/install-rocksdb.sh">
<summary>完整文件 · ch05/install-rocksdb.sh</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
bash "$LAB_ROOT/v8-mod-tutor/src/code/ch05/install-rocksdb.sh"
```

<div class="test-result"><strong>步骤测试 5.1 · RocksDB 静态构建</strong><dl>
<dt>预期输出</dt><dd>完成 344 个对象后出现 <code>Linking CXX static library librocksdb.a</code>，脚本末行是 <code>RocksDB 9.10.0:OK</code>。</dd>
<dt>原因</dt><dd>安装结果来自锁定 Clang 21/libc++，不是系统 <code>librocksdb-dev</code>；所有可选压缩库均关闭。</dd>
</dl></div>

## 2. 建立 Chapter 05 工程

本章复用第三章 Runtime，并链接刚安装的 `RocksDB::rocksdb` CMake target。

<details class="code-accordion" data-code="ch05/CMakeLists.txt">
<summary>完整文件 · ch05/CMakeLists.txt</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
CMAKE="$LAB_ROOT/.deps/cmake-3.31.8/bin/cmake"
SOURCE="$LAB_ROOT/v8-mod-tutor/src/code/ch05"
BUILD="$SOURCE/build-v137"

"$CMAKE" -S "$SOURCE" -B "$BUILD" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$LAB_ROOT/v8-mod-tutor/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$LAB_ROOT/.deps/v137"
"$CMAKE" --build "$BUILD"
```

<div class="test-result"><strong>步骤测试 5.2 · 链接存储运行时</strong><dl>
<dt>预期输出</dt><dd>最后链接 <code>libstorage_runtime.a</code>、<code>storage-test</code> 和 <code>v8-kv</code>。</dd>
<dt>原因</dt><dd>V8、KJ、RocksDB 和 storage binding 已在同一套 libc++ ABI 下组成可执行程序。</dd>
</dl></div>

## 3. 先定义物理 key

逻辑 namespace 和用户 key 编码为：

```text
<namespace> \0 <user-key>
```

NUL 是明确边界，所以 `one/x` 与 `two/x` 永远不会碰撞。namespace 最长 128 字节且不能含 NUL，key 最长 1 KiB，value 最长 1 MiB。

<details class="code-accordion" data-code="ch05/src/kv-store.h">
<summary>完整文件 · ch05/src/kv-store.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch05/src/kv-store.c++">
<summary>完整文件 · ch05/src/kv-store.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

KvStore 的 `get` 用 `std::optional` 区分“不存在”和空字符串；list 从编码后的 prefix Seek，离开前缀后立即停止。单次写默认启用 WAL，但 `sync=false`；需要强制刷盘时显式传 `true`。

<div class="test-result"><strong>步骤测试 5.3 · 编码与 CRUD/list</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] physical keys isolate namespaces</code> 和 <code>[PASS] KvStore supports put get delete and prefix list</code>。</dd>
<dt>原因</dt><dd>第一个 case 检查 NUL 的准确位置；第二个写入两个前缀 key、读取、列举、删除，再确认 deleted key 返回空 optional。</dd>
</dl></div>

## 4. 建立有界工作队列

同步 RocksDB 调用不能直接放在 V8/KJ 线程，否则一次磁盘抖动会同时冻结 HTTP、timer 和 WebSocket。StorageExecutor 的队列只保存拥有所有权的字符串与回调：

```text
V8 / KJ thread                    storage thread
--------------                   --------------
创建 KJ Promise
复制 key/value ────────────────→ RocksDB::Get/Put
                                 复制结果
cross-thread fulfiller ─────────→ 唤醒原 event loop
进入 Isolate
resolve JavaScript Promise
```

<details class="code-accordion" data-code="ch05/src/storage-executor.h">
<summary>完整文件 · ch05/src/storage-executor.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch05/src/storage-executor.c++">
<summary>完整文件 · ch05/src/storage-executor.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

> **线程硬规则。** worker lambda 只能携带字符串、数字和 CrossThreadPromiseFulfiller。`v8::Local`、`v8::Global`、Context、Isolate 与 cppgc 对象都不能进入 RocksDB 线程。

<div class="test-result"><strong>步骤测试 5.4 · 饱和时立即拒绝</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] StorageExecutor rejects work beyond its bound</code>。</dd>
<dt>原因</dt><dd>测试暂停单个 worker，以容量 2 提交三项；前两项留在队列，第三项立即以 <code>StorageBusyError</code> reject，随后恢复 worker 并正常 drain。</dd>
</dl></div>

## 5. 安装 env.KV

第三章 Runtime 增加一个可选 environment Global；没有 binding 时仍传空对象，有 KvBinding 时传入包含 KV 的同一个 env。

KvBinding 自身由 C++ Runtime 生命周期持有，JS 方法的 `v8::External` 只保存它的非拥有指针。关闭顺序固定为：先销毁 binding TaskSet，再停止并 join StorageExecutor，随后关闭 RocksDB，最后才销毁 Runtime/Isolate。

<details class="code-accordion" data-code="ch05/src/kv-binding.h">
<summary>完整文件 · ch05/src/kv-binding.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch05/src/kv-binding.c++">
<summary>完整文件 · ch05/src/kv-binding.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

JavaScript API 保持最小：

```js
await env.KV.put("room:lobby:topic", "V8 internals")
const topic = await env.KV.get("room:lobby:topic")
const page = await env.KV.list("room:", 100)
await env.KV.delete("room:lobby:topic")
```

`get` 缺失时 resolve 为 `null`；list 返回 `{ keys, complete }`。本章不加入 cursor，避免在还没有分页需求时伪造不稳定协议。

<div class="test-result"><strong>步骤测试 5.5 · JavaScript Promise 回传</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] env KV resolves JavaScript promises on the runtime thread</code>。</dd>
<dt>原因</dt><dd>worker 依次 await put、get、delete、get，最终 Response 是 <code>V8:null</code>。V8 对象只在 KJ continuation 回到 Runtime 后创建。</dd>
</dl></div>

## 6. 完整程序与示例 worker

<details class="code-accordion" data-code="ch05/src/main.c++">
<summary>完整文件 · ch05/src/main.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch05/worker/index.js">
<summary>完整文件 · ch05/worker/index.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

完整测试文件：

<details class="code-accordion" data-code="ch05/test/storage-test.c++">
<summary>完整文件 · ch05/test/storage-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
"$BUILD/storage-test"
"$LAB_ROOT/.deps/cmake-3.31.8/bin/ctest" --test-dir "$BUILD" --output-on-failure
```

<div class="test-result"><strong>步骤测试 5.6 · 五个存储 case</strong><dl>
<dt>预期输出</dt><dd>五行 <code>[PASS]</code>，汇总 <code>5 test(s) passed</code>；CTest 为 <code>1/1 ... Passed</code>。</dd>
<dt>原因</dt><dd>KJ runner 分别覆盖编码、同步数据库、跨线程背压、JS binding 和重开恢复；CTest 将整个 runner 视为一个测试程序。</dd>
</dl></div>

## 7. 重开验收

选择一个不会与现有数据库冲突的新目录，连续运行两次：

```bash
DB=/tmp/v8-kv-ch05
mkdir -p "$DB"
"$BUILD/v8-kv" "$SOURCE/worker/index.js" "$DB"
"$BUILD/v8-kv" "$SOURCE/worker/index.js" "$DB"
```

<div class="test-result"><strong>章节验收 · 持久化 KV</strong><dl>
<dt>预期输出</dt><dd>两次都输出 <code>V8 internals; keys=1</code>。</dd>
<dt>原因</dt><dd>第一次创建 DB 并写入 topic，第二次打开同一目录后覆盖同一 key；前缀 list 始终只看见一个持久化物理 key。</dd>
</dl></div>

当前逐项写 WAL、日志逐条 flush 的策略优先保证清晰和可靠。下一章会加入 trace 与负载测试，用数据决定怎样批量写入，而不是先猜优化参数。

<nav class="pager"><a href="../ch04/">← 第 4 章</a><a href="../ch06/">第 6 章：Trace 与优化 →</a></nav>
