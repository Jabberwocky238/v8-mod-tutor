---
title: 第 3 章 · Fetch Handler、定时器与 ExecutionContext
description: 用纯 C++、V8 和 KJ 实现最小 Fetch API，并让响应强制等待三秒
---

<p class="chapter-no">CHAPTER 03 · Fetch Runtime</p>

# 从求值器到 Fetch Handler

<p class="lead">上一章只能执行一段表达式。本章把它推进成可访问的 HTTP 服务：C++ 创建 Request，调用 JavaScript <code>worker.fetch(request, env, ctx)</code>，等待返回的 Promise，最后把 Response 写回客户端。</p>

> **本章新增：`Runtime`。** 它在第二章 Engine 的基础上长期持有 Isolate、Context、worker 和 CppHeap，并负责每一次 C++ → JavaScript 调用的 HandleScope、ContextScope、异常边界和微任务检查点。

> **本章新增：最小 Fetch API。** Request 暂时只有 `method`、`url`；Response 支持 body 和 status；env 传入空对象。本章不做中间件和存储，因为它们与理解一次请求的异步生命周期无关。

> **本章新增：`setTimeout` 与 `ExecutionContext`。** `setTimeout` 让 handler 真正异步等待；`ctx.waitUntil(promise)` 登记响应后的后台工作，但不会把它错误地拼进响应 Promise。

## 1. 先建立第三章工程

第三章是独立可编译工程，不覆盖第二章。目标分为运行时静态库、HTTP 程序和单元测试程序。

<details class="code-accordion" data-code="ch03/CMakeLists.txt">
<summary>完整文件 · ch03/CMakeLists.txt</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
CMAKE="$LAB_ROOT/.deps/cmake-3.31.8/bin/cmake"
SOURCE="$LAB_ROOT/v8-mod-tutor/src/code/ch03"
BUILD="$SOURCE/build-v137"

"$CMAKE" -S "$SOURCE" -B "$BUILD" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$LAB_ROOT/v8-mod-tutor/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$LAB_ROOT/.deps/v137"
"$CMAKE" --build "$BUILD"
```

<div class="test-result"><strong>步骤测试 3.1 · 构建三个目标</strong><dl>
<dt>预期输出</dt><dd>首次构建最后依次出现 <code>libfetch_runtime.a</code>、<code>fetch-runtime-test</code> 和 <code>v8-fetch</code> 的 Linking 行。</dd>
<dt>原因</dt><dd>这证明 Runtime 同时能被测试程序和真实 HTTP 程序复用，V8、CppGC、KJ async 与 KJ HTTP 均已完成链接。</dd>
</dl></div>

## 2. 把 CppHeap 接入 Isolate

`Runtime` 不再每个对象临时初始化 V8。Platform 是进程级单例，多个测试 fixture 各自创建 Isolate；每个 Isolate 又连接自己的 CppHeap。Platform 只在进程退出时释放，因为 V8 不允许 Dispose 后在同一进程重新初始化。

<details class="code-accordion" data-code="ch03/src/runtime.h">
<summary>完整文件 · ch03/src/runtime.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch03/src/runtime.c++">
<summary>完整文件 · ch03/src/runtime.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 3.2 · 进程与 Isolate 生命周期</strong><dl>
<dt>运行</dt><dd><code>"$BUILD/fetch-runtime-test"</code></dd>
<dt>预期输出</dt><dd>六个 case 连续运行，不出现 <code>kPlatformDisposed</code> 或 HandleScope fatal error。</dd>
<dt>原因</dt><dd>每个 fixture 都销毁自己的 Context、Isolate 和 CppHeap，但共享尚未释放的进程 Platform；异步回调也只在建立 HandleScope 后解引用 Global handle。</dd>
</dl></div>

## 3. 实现可取消的 setTimeout

TimerQueue 给每个回调分配数字 id，用 `v8::Global&lt;Function&gt;` 保持回调可达，再把 KJ timer Promise 放入 TaskSet。到期时先从表中移出回调，再进入 Runtime 调用；`clearTimeout(id)` 只需提前移除它。

<details class="code-accordion" data-code="ch03/src/timer-queue.h">
<summary>完整文件 · ch03/src/timer-queue.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch03/src/timer-queue.c++">
<summary>完整文件 · ch03/src/timer-queue.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 3.3 · 到期与取消</strong><dl>
<dt>预期输出</dt><dd>到期、取消和“带 pending timer 退出”三个 case 都显示 <code>[PASS]</code>。</dd>
<dt>原因</dt><dd>虚拟时钟推进到 29ms 时 active timer 仍为 1；第 30ms 回调执行并归零。取消测试证明被移除的 Global 没有被调用；退出测试证明剩余 Global 会在 Isolate 销毁前清空。</dd>
</dl></div>

## 4. 模拟 Fetch API

加载的脚本把 handler 放到 `globalThis.worker`。这避免提前引入 ES module loader，把注意力留在请求生命周期；第四章仍沿用这一约定。

<details class="code-accordion" data-code="ch03/worker/index.js">
<summary>完整文件 · ch03/worker/index.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

Runtime 为每个请求创建三个参数：带 method/url 的 Request 对象、空 env 对象、带 `waitUntil()` 的 ExecutionContext 对象。Response 构造器生成普通 JavaScript 对象；这已经足以教学和测试状态、正文及异步返回，不假装实现完整浏览器标准。

`dispatch()` 遇到同步 Response 就立即读取；遇到 Promise 则每个虚拟毫秒检查一次状态。定时器回调执行后，显式的 `PerformMicrotaskCheckpoint()` 会恢复 `async fetch()`。

<div class="test-result"><strong>步骤测试 3.4 · 强制等待三秒</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... fetch response waits three seconds</code>。</dd>
<dt>原因</dt><dd>时钟停在 2999ms 时 response Promise 的 <code>poll()</code> 为 false；推进到 3000ms 后返回 <code>200 / ready</code>。测试使用虚拟时间，所以不需要真的等待三秒。</dd>
</dl></div>

## 5. ExecutionContext 不阻塞响应

`ctx.waitUntil(promise)` 给 Promise 同时安装 fulfill/reject 回调，并增加后台任务计数。它不改变 fetch 返回的 Promise；后台 Promise settle 后计数才减一。

<details class="code-accordion" data-code="ch03/src/execution-context.h">
<summary>完整文件 · ch03/src/execution-context.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch03/src/execution-context.c++">
<summary>完整文件 · ch03/src/execution-context.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 3.5 · waitUntil 生命周期</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... waitUntil outlives the response</code>。</dd>
<dt>原因</dt><dd>响应正文 <code>sent</code> 已经可读时后台计数仍为 1；虚拟时钟再推进 250ms 后计数变成 0，因此后台工作属于请求上下文，但没有拖延响应。</dd>
</dl></div>

## 6. 把 Runtime 接到 KJ HTTP

FetchService 只做协议适配：将 KJ method/url 交给 Runtime，收到 FetchResponse 后设置 Content-Type、Content-Length 并写 body。JavaScript 细节不会泄漏到网络层。

<details class="code-accordion" data-code="ch03/src/fetch-service.h">
<summary>完整文件 · ch03/src/fetch-service.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch03/src/fetch-service.c++">
<summary>完整文件 · ch03/src/fetch-service.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch03/src/main.c++">
<summary>完整文件 · ch03/src/main.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

异常测试先让 fetch 抛出 `Error('boom')`，随后在同一个 Isolate 中加载正常 handler。

<div class="test-result"><strong>步骤测试 3.6 · 请求错误隔离</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... a thrown fetch does not poison the isolate</code>，最终汇总 <code>6 test(s) passed</code>。</dd>
<dt>原因</dt><dd>TryCatch 将异常转换为当前 dispatch 的 KJ exception；重新加载 handler 后仍返回 <code>ok</code>，说明进程级 V8 状态没有被一次坏请求破坏。</dd>
</dl></div>

完整测试文件如下。它使用 `kj::TimerImpl`，因此 30ms、250ms 和 3000ms 都是可精确推进的虚拟时间。

<details class="code-accordion" data-code="ch03/test/fetch-runtime-test.c++">
<summary>完整文件 · ch03/test/fetch-runtime-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
"$BUILD/fetch-runtime-test"
"$LAB_ROOT/.deps/cmake-3.31.8/bin/ctest" \
  --test-dir "$BUILD" --output-on-failure
```

## 7. 真实端口验收

启动服务：

```bash
"$BUILD/v8-fetch" "$SOURCE/worker/index.js" 127.0.0.1:8080
```

另一个终端请求 `/demo`：

```bash
curl --fail --silent --show-error \
  --write-out '\nstatus=%{http_code} starttransfer=%{time_starttransfer}s\n' \
  http://127.0.0.1:8080/demo
```

<div class="test-result"><strong>章节验收 · 真实三秒响应</strong><dl>
<dt>预期输出</dt><dd>正文为 <code>hello after 3 seconds: /demo</code>，状态为 200，<code>starttransfer</code> 约为 3.00 秒。</dd>
<dt>原因</dt><dd>真实 KJ timer 在三秒后 resolve，V8 微任务恢复 async fetch，FetchService 此时才调用 <code>response.send()</code>。随后登记的 250ms waitUntil 继续执行，不影响已经发出的响应。</dd>
</dl></div>

下一章不会推倒这个 Runtime，而是在它上面增加 SPA 分流、WebSocket、内存聊天室和持久日志。

<nav class="pager"><a href="../ch02/">← 第 2 章</a><a href="../ch04/">第 4 章：Chatroom →</a></nav>
