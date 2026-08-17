---
title: 第 4 章 · WebSocket Chatroom 与 SPA 分流
description: 在 Fetch Runtime 上加入 WebSocket、SPA、内存房间和追加日志
---

<p class="chapter-no">CHAPTER 04 · Chatroom</p>

# 做一个内存 Chatroom

<p class="lead">这一章不替换上一章的 Fetch Runtime，而是在它前面增加 Router。HTTP 提供 SPA 和 API，WebSocket 保持长连接；房间与最近消息存在内存，所有聊天事件同时追加到日志文件。</p>

> **本章新增：Router。** `/api/*` 继续调用第三章 Fetch handler，`/ws` 执行 WebSocket upgrade，静态资源按文件返回，其余无扩展名 GET 路径回退到 SPA。

> **本章新增：ChatHub。** 它是进程级 C++ 对象，保存房间成员和最近 50 条广播。状态在重启后消失；第五章引入 RocksDB 前，我们故意保留这个限制。

> **本章新增：WebSocketSession。** 每个升级连接有一个 session。读取循环接收文本，写入使用一条 Promise 链串行发送；排队超过 64 条时只用 1013 关闭慢连接。

> **本章新增：AppendLog。** join、message、leave 以 JSON Lines 立即 flush 到文件。逐条 flush 简单可靠但写入成本高，第六章会测量并优化它。

## 1. 从第三章增量构建

Chapter 04 的 CMake 直接复用 Chapter 03 的 Runtime 源文件，再加入聊天模块。这条依赖关系在工程里是可见的，不复制一份 Runtime 假装彼此独立。

<details class="code-accordion" data-code="ch04/CMakeLists.txt">
<summary>完整文件 · ch04/CMakeLists.txt</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
CMAKE="$LAB_ROOT/.deps/cmake-3.31.8/bin/cmake"
SOURCE="$LAB_ROOT/v8-mod-tutor/src/code/ch04"
BUILD="$SOURCE/build-v137"

"$CMAKE" -S "$SOURCE" -B "$BUILD" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$LAB_ROOT/v8-mod-tutor/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$LAB_ROOT/.deps/v137"
"$CMAKE" --build "$BUILD"
```

<div class="test-result"><strong>步骤测试 4.1 · 增量构建</strong><dl>
<dt>预期输出</dt><dd>最终链接 <code>libchat_runtime.a</code>、<code>chatroom-test</code> 和 <code>v8-chat</code>。</dd>
<dt>原因</dt><dd>第三章的 V8/Fetch 代码与本章 Router、ChatHub、WebSocketSession、AppendLog 已经进入同一个纯 C++ 程序。</dd>
</dl></div>

## 2. 先把路由边界写死

分流是纯函数，先于网络代码完成。未知前端页面可以回退到 `index.html`，缺失的 `.js`、`.css` 等带扩展名资源必须是 404，否则浏览器会尝试把 HTML 当脚本解析。

```text
/api/*                 FETCH
/ws                    WEBSOCKET
/app.js、/style.css     ASSET（文件必须存在）
/rooms/lobby           SPA
/missing.js            NOT_FOUND
非 GET 页面请求         METHOD_NOT_ALLOWED
```

<details class="code-accordion" data-code="ch04/src/router.h">
<summary>完整文件 · ch04/src/router.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/src/router.c++">
<summary>完整文件 · ch04/src/router.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 4.2 · 六类路由</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... router separates API WebSocket assets and SPA</code>。</dd>
<dt>原因</dt><dd>测试逐一断言 FETCH、WEBSOCKET、ASSET、SPA、NOT_FOUND 和 METHOD_NOT_ALLOWED；路由错误不会等到浏览器才暴露。</dd>
</dl></div>

## 3. 实现内存房间

ChatClient 是 ChatHub 与具体 socket 之间的窄接口。join 分配稳定 id，broadcast 给同房间每位成员 enqueue，并把服务器生成的 JSON 保存到最多 50 条的历史队列；leave 幂等删除成员。

浏览器只发送 UTF-8 文本，例如 `hello`。服务器统一生成结构化广播：

```json
{"type":"message","room":"lobby","user":"alice","text":"hello"}
```

这样服务端不需要为本章临时手写 JSON parser；所有输出则经过唯一的 `jsonEscape()`，用户名、房间和消息不能破坏 JSON 结构。

<details class="code-accordion" data-code="ch04/src/chat-hub.h">
<summary>完整文件 · ch04/src/chat-hub.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/src/chat-hub.c++">
<summary>完整文件 · ch04/src/chat-hub.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 4.3 · 广播、历史与离开</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... ChatHub broadcasts and keeps in-memory history</code>。</dd>
<dt>原因</dt><dd>alice 和 bob 各收到一次 hello，房间历史增加一条；bob 离开后成员数从 2 变成 1。</dd>
</dl></div>

## 4. 串行写 WebSocket

KJ 明确禁止同一 WebSocket 同时执行多个 `send()`。Session 用 `outgoing` Promise 链把广播逐条串起来；每条消息的拥有者通过 `attach()` 活到 send 完成。

<details class="code-accordion" data-code="ch04/src/websocket-session.h">
<summary>完整文件 · ch04/src/websocket-session.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/src/websocket-session.c++">
<summary>完整文件 · ch04/src/websocket-session.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

WebSocket upgrade 还有一条容易忽略的所有权规则：`acceptWebSocket()` 得到的对象必须由当前 `HttpService::request()` 返回的 Promise 持有。因此 `/ws` 返回 `session->run().attach(session)`，不能把 session 扔进旁路 TaskSet 后立即返回。

<div class="test-result"><strong>步骤测试 4.4 · 慢连接隔离</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... a slow client is removed without blocking its room</code>。</dd>
<dt>原因</dt><dd>容量为 1 的 slow client 在第二条广播时得到 1013 并离开；fast client 仍收到两条消息，房间没有被一个消费者堵住。</dd>
</dl></div>

## 5. 保存因果日志

日志模块创建父目录，以 append 模式打开文件，每个事件生成一行 JSON 并立即 flush。

<details class="code-accordion" data-code="ch04/src/append-log.h">
<summary>完整文件 · ch04/src/append-log.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/src/append-log.c++">
<summary>完整文件 · ch04/src/append-log.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 4.5 · JSONL 顺序与转义</strong><dl>
<dt>预期输出</dt><dd><code>[PASS] ... chat log preserves join message leave order</code>。</dd>
<dt>原因</dt><dd>临时日志中 join 位于 message 前、message 位于 leave 前；消息里的换行保存为 <code>\n</code>，没有截断 JSONL 记录。</dd>
</dl></div>

## 6. 组合 HTTP、SPA 与 WebSocket

RouterService 执行真正的协议分流和静态文件读取，main 组装 Runtime、日志、ChatHub、RouterService 与 KJ HttpServer。

<details class="code-accordion" data-code="ch04/src/router-service.h">
<summary>完整文件 · ch04/src/router-service.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/src/router-service.c++">
<summary>完整文件 · ch04/src/router-service.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/src/main.c++">
<summary>完整文件 · ch04/src/main.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

## 7. 完整 SPA

页面、样式和浏览器逻辑都是独立文件。两个窗口只要 room 相同就会收到同一广播。

<details class="code-accordion" data-code="ch04/public/index.html">
<summary>完整文件 · ch04/public/index.html</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/public/style.css">
<summary>完整文件 · ch04/public/style.css</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/public/app.js">
<summary>完整文件 · ch04/public/app.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

完整 C++ 测试与双客户端 smoke test：

<details class="code-accordion" data-code="ch04/test/chatroom-test.c++">
<summary>完整文件 · ch04/test/chatroom-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<details class="code-accordion" data-code="ch04/test/websocket-smoke.js">
<summary>完整文件 · ch04/test/websocket-smoke.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
"$BUILD/chatroom-test"
"$LAB_ROOT/.deps/cmake-3.31.8/bin/ctest" --test-dir "$BUILD" --output-on-failure
```

<div class="test-result"><strong>步骤测试 4.6 · 本章单元测试</strong><dl>
<dt>预期输出</dt><dd>四个 case 分别显示 <code>[PASS]</code>，最终为 <code>4 test(s) passed</code>；CTest 显示 <code>1/1 ... Passed</code>。</dd>
<dt>原因</dt><dd>KJ 按 case 计数，CTest 按测试可执行文件计数，与第二、三章的规则相同。</dd>
</dl></div>

## 8. 真实服务验收

```bash
"$BUILD/v8-chat" \
  "$LAB_ROOT/v8-mod-tutor/src/code/ch03/worker/index.js" \
  "$SOURCE/public" \
  "$LAB_ROOT/v8-mod-tutor/data/chat.log" \
  127.0.0.1:8080
```

另一个终端：

```bash
curl -i http://127.0.0.1:8080/rooms/lobby
curl -i http://127.0.0.1:8080/missing.js
node "$SOURCE/test/websocket-smoke.js" ws://127.0.0.1:8080/ws
tail -n 5 "$LAB_ROOT/v8-mod-tutor/data/chat.log"
```

<div class="test-result"><strong>章节验收 · SPA、双客户端与日志</strong><dl>
<dt>预期输出</dt><dd>SPA 路由为 200，缺失脚本为 404；smoke test 输出 <code>2 clients received alice: hello</code>；日志依次出现两次 join、一次 message、两次 leave。</dd>
<dt>原因</dt><dd>两个真实 WebSocket 连接进入同一 ChatHub 房间，alice 的文本经过统一 JSON 编码和两条独立 writer chain 广播；关闭连接时 session 各自执行幂等 leave。</dd>
</dl></div>

下一章用 RocksDB 替换“重启即丢失”的部分，但 WebSocket session 本身仍只存在内存。

<nav class="pager"><a href="../ch03/">← 第 3 章</a><a href="../ch05/">第 5 章：RocksDB KV →</a></nav>
