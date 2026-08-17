---
title: 第 6 章 · Trace、测量与批量写
description: 用有界 Trace 记录性能，再用 RocksDB WriteBatch 减少物理写调用
---

<p class="chapter-no">CHAPTER 06 · Trace</p>

# 先看见瓶颈，再优化写入

<p class="lead">上一章已经能持久化 KV，但“能够写入”和“知道写得怎么样”是两回事。本章先记录一段工作的父子关系和耗时，再把许多小写入合成 RocksDB WriteBatch，最后用同一个可执行 benchmark 比较两条路径。</p>

> **本章新增：`TraceContext` 与 `Span`。** 一个 context 表示一条 trace；每个 RAII span 记录名称、父 span、单调时钟开始时间、耗时和状态。作用域异常退出时，析构函数仍会补一条 `unset` 记录。

> **本章新增：`TraceWriter`。** 业务线程只把记录放进有界内存队列，后台线程分批追加 JSONL。队列满时增加 `dropped` 并立即返回，磁盘变慢不会无限占用内存。

> **本章新增：`WriteOperation` 与 `KvStore::writeBatch()`。** 一组 put/delete 由一个 RocksDB `WriteBatch` 原子提交，继续沿用第五章的 namespace、key 和 value 限制。

> **本章新增：`WriteCoordinator`。** 它按操作数或字节数形成批次，并公开 `operations`、`writeCalls` 两个计数器，让优化可以被测试，而不是靠感觉判断。

> **本章新增：`write-benchmark`。** 它对两个新数据库写入完全相同的 10,000 个 1 KiB value，分别测逐条 put 和每 128 条一批。它是独立程序，不依赖不存在的 HTTP API 或外部压测工具。

## 1. 建立本章工程

本章只需要第五章的 `KvStore`、锁定版本的 KJ 和 RocksDB。`Trace` 本身不接触 V8 handle；以后把 span 放进请求、定时器或存储 continuation 时，仍需遵守“V8 handle 不跨线程”的规则。

<details class="code-accordion" data-code="ch06/CMakeLists.txt">
<summary>完整文件 · ch06/CMakeLists.txt</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
CMAKE="$LAB_ROOT/.deps/cmake-3.31.8/bin/cmake"
SOURCE="$LAB_ROOT/v8-mod-tutor/src/code/ch06"
BUILD="$SOURCE/build-v137"

"$CMAKE" -S "$SOURCE" -B "$BUILD" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$LAB_ROOT/v8-mod-tutor/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$LAB_ROOT/.deps/v137"
"$CMAKE" --build "$BUILD"
```

<div class="test-result"><strong>步骤测试 6.1 · 构建独立工程</strong><dl>
<dt>预期输出</dt><dd>最后出现 <code>Linking CXX executable trace-test</code> 和 <code>Linking CXX executable write-benchmark</code>。</dd>
<dt>原因</dt><dd>两个程序都由锁定的 Clang 21、libc++、KJ 与 RocksDB 静态库完成链接，本章代码没有退回系统 C++ 依赖。</dd>
</dl></div>

## 2. 定义 Trace 数据模型

Trace ID 标识整条工作链；span ID 标识其中一步；`parentSpanId` 把步骤连成树。时间使用 `steady_clock`，因为系统时间被校准或手工修改时，单调时钟仍能正确计算耗时。

<details class="code-accordion" data-code="ch06/src/trace.h">
<summary>完整文件 · ch06/src/trace.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

`TraceContext::span(name, parentId)` 创建 span。根 span 的 parent 是 0；子 span 显式接收父 ID。显式传递比线程局部变量更适合异步 Runtime，因为 continuation 不一定立刻执行。

## 3. 用 RAII 收尾 Span

<details class="code-accordion" data-code="ch06/src/trace.c++">
<summary>完整文件 · ch06/src/trace.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

`finish()` 先检查 `finished`，所以显式结束后，析构不会重复提交。测试注入一个返回整数纳秒的时钟，避免真的等待 30 ms：时钟从 1,000 前进到 30,001,000，记录必须恰好是 30,000,000 ns。

<div class="test-result"><strong>步骤测试 6.2 · 父子关系、耗时与析构兜底</strong><dl>
<dt>运行</dt><dd><code>"$BUILD/trace-test"</code></dd>
<dt>预期输出</dt><dd><code>[PASS] nested spans preserve trace and parent identifiers</code>、<code>[PASS] span duration uses a monotonic clock and finish is idempotent</code>、<code>[PASS] span destructor records an unfinished scope</code>。</dd>
<dt>原因</dt><dd>父子 span 共享 trace ID，child 的 parent 是 root；注入时钟让耗时完全确定；离开作用域而未调用 finish 时，析构提交状态为 unset 的记录。</dd>
</dl></div>

## 4. 有界队列与 JSONL

`submit()` 只持锁移动一个 `TraceRecord`，不打开文件。后台 writer 从队列取最多 `batchSize` 条，再一次打开并追加文件。关闭时先唤醒并 join 线程，最后 drain 剩余记录。

每行是一条完整 JSON，对进程崩溃后的局部恢复和流式分析都更友好：

```json
{"trace_id":"...","span_id":2,"parent_span_id":1,"name":"rocksdb.write","started_ns":1000,"duration_ns":30000000,"status":"ok"}
```

测试把后台线程关闭，容量设为 2，确保第三次 submit 稳定失败。另一个测试主动 flush 两条记录，检查换行转义和提交顺序。

<div class="test-result"><strong>步骤测试 6.3 · 队列上限与 JSONL</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] TraceWriter drops at capacity without blocking the caller</code> 和 <code>[PASS] TraceWriter writes escaped JSONL records in submission order</code>。</dd>
<dt>原因</dt><dd>暂停消费后容量只有 2，第三条使 dropped 变成 1；flush 后文件恰有两行，名称中的换行被写成 <code>\n</code>，第二行仍是 span 2。</dd>
</dl></div>

## 5. 给 KvStore 增加原子批次

<details class="code-accordion" data-code="ch05/src/kv-store.h">
<summary>完整文件 · ch05/src/kv-store.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch05/src/kv-store.c++">
<summary>完整文件 · ch05/src/kv-store.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

每个操作仍先经过 `encodeKey()` 和大小检查，再进入 `rocksdb::WriteBatch`。`DB::Write()` 只调用一次；失败时整个 batch 失败，不会向其中某一项假报成功。默认仍保留 WAL 且 `sync=false`，持久性语义与上一章一致。

## 6. 按阈值形成批次

<details class="code-accordion" data-code="ch06/src/write-coordinator.h">
<summary>完整文件 · ch06/src/write-coordinator.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch06/src/write-coordinator.c++">
<summary>完整文件 · ch06/src/write-coordinator.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

默认上限是 128 个操作或 1 MiB。达到任一阈值就 flush；一轮工作结束也必须显式 flush，析构只作为异常路径兜底。真实服务中仍把 coordinator 放到第五章的存储工作线程，不要在 V8/KJ event loop 上执行 RocksDB 写入。

<div class="test-result"><strong>步骤测试 6.4 · 阈值、读取与重启恢复</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] WriteCoordinator flushes at the operation threshold</code> 和 <code>[PASS] batched data and deletes survive reopening RocksDB</code>。</dd>
<dt>原因</dt><dd>前两个 put 不写数据库，第三个使 writeCalls 从 0 变成 1；另一个 case 用一个 batch 完成 put 与 delete，关闭并重开 RocksDB 后，新值仍在、旧值已删除。</dd>
</dl></div>

## 7. 测量，而不是猜测

<details class="code-accordion" data-code="ch06/src/write-benchmark.c++">
<summary>完整文件 · ch06/src/write-benchmark.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
"$BUILD/write-benchmark" 10000
```

本仓库在 2026-08-17 的一次 Debug 构建实测为：

```text
mode=individual operations=10000 write_calls=10000 elapsed_ms=27
mode=batch128 operations=10000 write_calls=79 elapsed_ms=15
```

79 等于 `ceil(10000 / 128)`，这是不受机器速度影响的结构性结果。27 ms 和 15 ms 会受 CPU、文件系统、后台负载及 Debug/Release 构建影响，不能写进单元测试，也不能承诺为固定加速比。正式比较应使用 Release 构建，多轮预热并报告中位数与尾延迟。

<div class="test-result"><strong>步骤测试 6.5 · 真实批量写基线</strong><dl>
<dt>预期输出</dt><dd>第一行 <code>write_calls=10000</code>，第二行 <code>write_calls=79</code>；两行的 <code>operations</code> 都是 10000。</dd>
<dt>原因</dt><dd>两组写入数据量相同，差别只在提交方式。最后不足 128 条的 16 个操作由显式 flush 形成第 79 次写。</dd>
</dl></div>

## 8. 一次运行全部验证

<details class="code-accordion" data-code="ch06/test/trace-test.c++">
<summary>完整文件 · ch06/test/trace-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
"$CMAKE" --build "$BUILD"
"$CMAKE" --build "$BUILD" --target test
```

<div class="test-result"><strong>步骤测试 6.6 · 七个 case 全部通过</strong><dl>
<dt>预期输出</dt><dd><code>100% tests passed, 0 tests failed out of 1</code>。其中一个 CTest 可执行文件内部运行 7 个 KJ case。</dd>
<dt>原因</dt><dd>Trace 的关系、时钟、RAII、容量、序列化，以及 WriteBatch 的阈值和重启恢复都已经被真实文件与真实 RocksDB 覆盖。</dd>
</dl></div>

本章的优化闭环是：先用 span 区分排队和写入耗时，再观察有界队列的 dropped，最后减少可计数的 RocksDB 写调用。吞吐数字只是证据的一部分；数据正确、内存有界、持久性不退化，优化才成立。

<nav class="pager"><a href="../ch05/">← 第 5 章</a><a href="../reference/">模块参考 →</a></nav>
