---
title: 第 3 章 · Fetch Handler、定时器与 ExecutionContext
description: 用 KJ HTTP 服务调用 JavaScript fetch handler，并实现 setTimeout 与 waitUntil 生命周期
---

<p class="chapter-no">CHAPTER 03 · Fetch Runtime</p>

# 从 Hello World 到 Fetch Handler

<p class="lead">这一章把短命的命令行程序改造成 HTTP 服务。请求进入 C++，转换为 JS Request，调用导出的 <code>fetch(request, env, ctx)</code>，等待结果后再写回 KJ Response。</p>

> **本章新增：`Runtime`。** 它长期拥有 Isolate、Context、编译后的 handler 和 cppgc heap。KJ 网络事件与 V8 共用一个线程，任何回调进入 JS 前都经过 `Runtime::enter()`。

> **本章新增：`Request` / `Response`。** 它们是最小 Web API，只实现 method、url、headers、text body、status 和响应 headers。env、中间件和存储故意留空。

> **本章新增：`TimerQueue`。** `setTimeout()` 将 JS 回调保存为 `v8::TracedReference<v8::Function>`，再用 `kj::Timer::afterDelay()` 唤醒。定时器触发后重新进入 Context、调用函数并执行微任务检查点。

> **本章新增：`ExecutionContext`。** `ctx.waitUntil(promise)` 收集响应后的任务。它不延迟 fetch 响应；只有 handler 自己 `await` 的 Promise 才会推迟响应。

## 1. 本章文件

```text
runtime/src/
├── main.c++                 启动 KJ 网络与 HTTP server
├── runtime.{h,c++}          Isolate、Context、模块与微任务
├── bindings/
│   ├── request.{h,c++}
│   ├── response.{h,c++}
│   ├── execution-context.{h,c++}
│   └── timers.{h,c++}
└── http-service.{h,c++}     KJ HTTP ↔ JS fetch
runtime/worker/index.js
```

完整职责和所有权关系见[请求生命周期参考](../reference/request-lifecycle/)。

本章所有步骤测试集中在一个独立文件。展开后可查看或复制完整内容：

<details class="code-accordion" data-code="ch03/test/fetch-runtime-test.c++">
<summary>完整测试 · ch03/test/fetch-runtime-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

## 2. 给 Isolate 接上 cppgc

第 2 章的 `CreateParams` 改为：

```cpp
auto cppHeap = v8::CppHeap::Create(
    platform.get(), v8::CppHeapCreateParams({}));

v8::Isolate::CreateParams params;
params.array_buffer_allocator =
    v8::ArrayBuffer::Allocator::NewDefaultAllocator();
params.cpp_heap = cppHeap.release();
isolate = v8::Isolate::New(params);
```

JS 可见对象继承 `v8::Object::Wrappable`：

```cpp
class Request final : public v8::Object::Wrappable {
 public:
  Request(kj::String method, kj::String url)
      : method(kj::mv(method)), url(kj::mv(url)) {}

  void Trace(cppgc::Visitor* visitor) const override {
    v8::Object::Wrappable::Trace(visitor);
  }

  kj::String method;
  kj::String url;
};
```

使用 `cppgc::MakeGarbageCollected<Request>()` 分配，并用 `v8::Object::Wrap()` 绑定到 JS wrapper。这样 JS wrapper 可达时 C++ 对象也可达，不再手写 weak callback 删除对象。

<div class="test-result"><strong>步骤测试 3.1 · cppgc wrapper</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R fetch-runtime</code></dd>
<dt>预期输出</dt><dd>case 显示 <code>PASS</code>，进程退出码为 0。</dd>
<dt>原因</dt><dd>JS wrapper 存在时统一 heap 能追踪 Request；移除 wrapper 并 GC 后 weak probe 才失效。</dd>
</dl></div>

## 3. 定义 Worker 接口

<details class="code-accordion" data-code="ch03/worker/index.js">
<summary>完整文件 · ch03/worker/index.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

这里有两个时钟：3 秒 Promise 被 `await`，客户端必须等待；250ms Promise 交给 `waitUntil()`，响应发出后继续运行。

## 4. 实现 setTimeout

绑定回调先检查参数，再创建可取消任务。完整接口定义：

<details class="code-accordion" data-code="ch03/src/timer-queue.h">
<summary>完整文件 · ch03/src/timer-queue.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

实际代码不要对脚本调用使用 `ToLocalChecked()`；用 `TryCatch` 将异常交给当前请求。上面片段只突出所有权：`TracedReference` 必须活到 KJ timer 完成，timer 取消时它也要释放。

> **线程规则。** `kj::Timer` 的 continuation 在本事件循环线程运行，因此可以重新进入 isolate。以后 RocksDB 使用工作线程时，只能把纯 C++ 数据传回 KJ 线程，绝不能把 `v8::Local` 或 `v8::Global` 送进线程池。

<div class="test-result"><strong>步骤测试 3.2 · setTimeout 与取消</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R fetch-runtime</code></dd>
<dt>预期输出</dt><dd>2 个 case 均 <code>PASS</code>。</dd>
<dt>原因</dt><dd>fake timer 前进到 29ms 时 Promise 仍 pending，第 30ms 才 resolve；已 clear 的回调不会运行且 active count 回到 0。</dd>
</dl></div>

## 5. 等待 JavaScript Promise

调用 handler 后可能得到普通 Response，也可能得到 Promise。`Runtime::awaitJs()` 将 Promise 的完成状态与 `kj::Promise` 桥接：

```text
调用 fetch
   ↓
返回 Response ───────────────→ 写 HTTP 响应
   │
   └─ 返回 Promise → KJ loop 等待 timer/I/O
                         ↓
                  microtask checkpoint
                         ↓
                  fulfilled Response → 写 HTTP 响应
```

桥接器给 JS Promise 安装 fulfill/reject 回调，回调只完成一个 `kj::PromiseFulfiller<v8::Global<v8::Value>>`。每次 KJ 事件进入 JS 后都执行 `PerformMicrotaskCheckpoint()`，否则 `async fetch()` 会停在已完成但未消费的 Promise 上。

<div class="test-result"><strong>步骤测试 3.3 · Fetch Promise 桥接</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R fetch-runtime</code></dd>
<dt>预期输出</dt><dd>2999ms 检查仍 pending，推进到 3000ms 后得到 <code>200 / ready</code>，case 为 <code>PASS</code>。</dd>
<dt>原因</dt><dd>KJ timer 到期后调用 resolve，microtask checkpoint 恢复 async fetch，最终 Response 才进入 HTTP 层。</dd>
</dl></div>

## 6. ExecutionContext 的结束规则

`ExecutionContext` 保存请求级 `kj::TaskSet`：

<details class="code-accordion" data-code="ch03/src/execution-context.h">
<summary>完整文件 · ch03/src/execution-context.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

HTTP 响应写完后，服务将 `ctx->drain()` 放进进程级后台 `TaskSet`，最长等待 30 秒。请求对象在这些任务结束前保持可达；超时后取消余下任务并写错误日志。

<div class="test-result"><strong>步骤测试 3.4 · waitUntil</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R fetch-runtime</code></dd>
<dt>预期输出</dt><dd>响应体立即是 <code>sent</code>，后台任务数先为 1，推进 250ms 后为 0。</dd>
<dt>原因</dt><dd>waitUntil Promise 进入后台 TaskSet，没有被加入 response Promise；完成后 TaskSet 自动移除它。</dd>
</dl></div>

## 7. KJ HTTP 入口

`FetchService` 实现 `kj::HttpService`：

```cpp
kj::Promise<void> request(kj::HttpMethod method,
                          kj::StringPtr url,
                          const kj::HttpHeaders& headers,
                          kj::AsyncInputStream& body,
                          Response& response) override;
```

处理顺序固定：限制 body 大小 → 创建 cppgc Request/ExecutionContext → 调用 handler → 等待 Response → `response.send()` → 后台 drain waitUntil。

<div class="test-result"><strong>步骤测试 3.5 · 错误隔离</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R fetch-runtime</code></dd>
<dt>预期输出</dt><dd>第一个请求状态 500；重新加载正常 handler 后第二个请求返回 <code>ok</code>，case 为 <code>PASS</code>。</dd>
<dt>原因</dt><dd>TryCatch 将异常限制在当前请求，没有终止 Isolate 或 KJ event loop。</dd>
</dl></div>

## 8. 构建与验收

```bash
cmake --build runtime/build
./runtime/build/v8lab --script runtime/worker/index.js --listen 127.0.0.1:8080
```

另一个终端：

```bash
curl -i -w '\nstarttransfer=%{time_starttransfer}s\n' http://127.0.0.1:8080/
```

验收结果：

- 首字节时间至少约 3 秒；响应体为 `hello after 3 seconds`。
- 响应发出约 250ms 后，服务日志出现后台任务信息。
- handler 抛异常时返回 500，进程不退出。
- 客户端断开时，请求 Promise 被取消；已登记的 waitUntil 按既定策略继续或超时。

<nav class="pager"><a href="../ch02/">← 第 2 章</a><a href="../ch04/">第 4 章：Chatroom →</a></nav>
