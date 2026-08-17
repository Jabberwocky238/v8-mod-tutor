---
title: 第 6 章 · Trace、压测与写入优化
description: 跟踪请求和存储阶段，建立性能基线，并用 RocksDB WriteBatch 优化写入吞吐
---

<p class="chapter-no">CHAPTER 06 · Trace</p>

# 用 Trace 找到写入瓶颈

<p class="lead">最后一章不再增加产品功能。我们给已有请求建立时间线，区分排队、JavaScript、定时器、RocksDB 和日志开销，再根据数据优化写入路径。</p>

> **本章新增：`TraceContext`。** 每个请求生成 trace id，维护单调时钟时间线；异步任务显式携带上下文，不能依赖线程局部变量，因为 continuation 可能稍后运行。

> **本章新增：`Span`。** RAII 对象记录开始、结束、状态和少量属性。析构只提交内存记录，不做磁盘 I/O。

> **本章新增：`TraceWriter`。** 有界队列加单 writer，将 span 批量写入 `data/traces.jsonl`。队列满时丢弃并累计 dropped 指标，不能反向拖慢业务路径。

> **本章新增：`WriteCoordinator`。** 在很短的批处理窗口内合并独立 KV 写入，使用 RocksDB `WriteBatch` 一次提交并分别完成原始 Promise。

<details class="code-accordion" data-code="ch06/test/trace-test.c++">
<summary>完整测试 · ch06/test/trace-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

## 1. Span 模型

一次第 3 章的请求至少产生：

```text
http.request                 0ms ───────────────────────── 3004ms
  js.fetch                   1ms ──────────────────────── 3002ms
    timer.wait               2ms ───────────────────── 3001ms
  http.write                                              2ms
waitUntil.background                                     ─── 252ms
```

KV 请求再增加：

```text
kv.operation
  storage.queue
  rocksdb.get / rocksdb.write
```

分别记录 queue time 和 service time。只看总耗时无法判断是 RocksDB 慢，还是线程池排队。

## 2. RAII Span

<details class="code-accordion" data-code="ch06/src/trace.h">
<summary>完整文件 · ch06/src/trace.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

显式 `finish()` 和析构兜底都必须幂等。异常路径同样写 span，但只记录错误类别，不把聊天文本、KV value 或完整 URL query 写进 trace。

<div class="test-result"><strong>步骤测试 6.1 · Span 父子关系与耗时</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R trace</code></dd>
<dt>预期输出</dt><dd><code>nested spans preserve... PASS</code> 与 <code>span duration uses... PASS</code>。</dd>
<dt>原因</dt><dd>child 继承 traceId 并记录 parentSpanId；fake monotonic clock 前进 30ms，所以 duration 精确为 30ms。</dd>
</dl></div>

## 3. 上下文传播

HTTP 入口创建根 trace：

```cpp
auto trace = TraceContext::start(random, timer);
auto span = trace.span("http.request");
return runtime.dispatch(kj::mv(trace), ...)
    .attach(kj::mv(span));
```

定时器、waitUntil、WebSocket 消息和 storage job 都显式复制轻量 `TraceToken`。后台线程只记录整数时间戳和已脱敏属性，最后交给 writer。

## 4. 建立基线

不要先改 RocksDB 选项。使用固定机器、固定 value 大小和固定并发，分别测：

```bash
# 纯 HTTP/JS，不访问 KV
wrk -t4 -c64 -d30s http://127.0.0.1:8080/api/ping

# 每请求一次 1 KiB put
wrk -t4 -c64 -d30s -s runtime/test/put.lua http://127.0.0.1:8080/api/kv
```

收集吞吐、错误率、p50/p95/p99、storage queue p95、RocksDB write p95、trace dropped 数和进程 RSS。每组先预热 10 秒，至少重复三次。

> **压测边界。** 第 3 章故意等待 3 秒的示例不能用于吞吐基线；为压测单独提供无 timer 的 `/api/ping` 和 `/api/kv`。否则测到的是教学延迟，不是运行时能力。

## 5. 先优化 trace 写入

错误做法是在每个 span 结束时 `write()` + `fsync()`。正确路径：

1. 业务线程把定长 `TraceRecord` 放进有界队列。
2. writer 最多等待 10ms 或累计 256 条。
3. 一次序列化成连续 buffer，执行一次 append。
4. 每秒 flush；崩溃最多丢失这一秒 trace，业务数据不受影响。

测量开启 trace 前后 `/api/ping` 吞吐。默认采样率先设 10%，错误请求始终采样。

<div class="test-result"><strong>步骤测试 6.2 · Trace 队列有界</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R trace</code></dd>
<dt>预期输出</dt><dd><code>TraceWriter drops at capacity... PASS</code>，queue depth 为 2，dropped 为 1。</dd>
<dt>原因</dt><dd>writer 被暂停且容量只有 2，第三条记录按策略丢弃，提交线程没有等待磁盘。</dd>
</dl></div>

## 6. 用 WriteBatch 合并 KV 写入

`WriteCoordinator` 的批处理条件：最多等待 1ms、最多 128 个操作、最多 1 MiB。任何条件满足就提交：

<details class="code-accordion" data-code="ch06/src/write-coordinator.h">
<summary>完整文件 · ch06/src/write-coordinator.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```cpp
rocksdb::WriteBatch batch;
for (auto& op : pending) {
  if (op.isPut()) batch.Put(kvHandle, op.key(), op.value());
  else batch.Delete(kvHandle, op.key());
}
auto status = db.Write(writeOptions, &batch);
completeAll(pending, status);
```

一次 WriteBatch 原子提交。批内某个操作格式错误必须在入队前拒绝；进入 batch 后只能整体成功或整体失败。

<div class="test-result"><strong>步骤测试 6.3 · 批量阈值</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R trace</code></dd>
<dt>预期输出</dt><dd><code>WriteCoordinator flushes at the operation threshold ... PASS</code>。</dd>
<dt>原因</dt><dd>前两个 put 未达阈值，write call 为 0；第三个加入后形成一次 WriteBatch，三个原 Promise 一起 fulfilled。</dd>
</dl></div>

## 7. WAL 与 sync 的取舍

比较三组，而不是直接关闭安全选项：

| 模式 | 语义 | 预期代价 |
| --- | --- | --- |
| WAL on, `sync=false` | 默认，进程崩溃可恢复；断电有窗口 | 常规 |
| WAL on, `sync=true` | 每批强制落盘 | 延迟最高 |
| WAL off | 依赖 memtable/SST，崩溃可能丢数据 | 仅可重建数据 |

聊天室 topic 属于用户数据，默认保留 WAL。trace 本身允许有限丢失，应走独立追加日志，不要与 KV 的持久性策略捆绑。

<div class="test-result"><strong>步骤测试 6.4 · 批量失败传播</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R trace</code></dd>
<dt>预期输出</dt><dd><code>a failed WriteBatch rejects every member ... PASS</code>。</dd>
<dt>原因</dt><dd>WriteBatch 是整体提交；模拟 disk full 后，批内两个 Promise 都收到同一失败，没有一项假报成功。</dd>
</dl></div>

## 8. 验证优化有效

最终报告至少包含：

```text
metric                 before       after
write requests/s       ...          ...
http p99               ... ms       ... ms
storage queue p95      ... ms       ... ms
rocksdb write p95      ... ms       ... ms
trace dropped          ...          ...
```

只有在错误率不升、内存有界、持久性语义不变时，吞吐提升才算成立。若 p50 变好但 p99 因 1ms 聚合窗口恶化，需要减小窗口或只在队列有压力时批处理。

完整字段、采样和关闭顺序见[Trace 参考](../reference/trace/)。

## 9. 最终验收

- HTTP、3 秒 timer、waitUntil、SPA 和 WebSocket 行为没有回归。
- 重启后 KV 可恢复，聊天室活跃连接按设计丢失。
- 每次请求能通过 trace id 串起 HTTP、JS、timer、storage 和日志阶段。
- trace writer 或 RocksDB 变慢时队列保持有界，并产生可见压力指标。
- 优化报告能说明性能变化来自哪里，而不只给一个更大的 QPS 数字。

<nav class="pager"><a href="../ch05/">← 第 5 章</a><a href="../reference/">模块参考 →</a></nav>
