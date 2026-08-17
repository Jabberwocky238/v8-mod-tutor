---
title: 第 4 章 · WebSocket Chatroom 与 SPA 分流
description: 在 Fetch Runtime 上加入 WebSocket、静态 SPA、内存聊天室和追加日志
---

<p class="chapter-no">CHAPTER 04 · Chatroom</p>

# 做一个内存 Chatroom

<p class="lead">这一章第一次让请求脱离“收一个、回一个”的模型。HTTP 负责 SPA，WebSocket 负责长连接，聊天室状态保存在进程内存，所有加入、消息和离开事件写入日志。</p>

> **本章新增：`RouterService`。** `/api/*` 进入 JS fetch，`/ws` 执行 WebSocket upgrade，其余 GET 请求从 `public/` 提供 SPA，并将未知前端路由回退到 `index.html`。

> **本章新增：`ChatHub`。** 它是进程级内存对象，按 room 保存连接。它不放进 JS heap，也不属于单次请求；每条连接通过稳定 ID 与它交互。

> **本章新增：`WebSocketSession`。** 它拥有 `kj::WebSocket` 的读写循环，负责帧大小、消息类型、背压和关闭握手。WebSocket 协议交给 KJ，不手写握手与帧解析。

> **本章新增：`AppendLog`。** 单写者 JSON Lines 日志。聊天状态仍是内存数据，但日志让重启前发生过什么可审计。

<details class="code-accordion" data-code="ch04/test/chatroom-test.c++">
<summary>完整测试 · ch04/test/chatroom-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

## 1. 路由边界

```text
GET /ws?room=lobby&name=lin  → WebSocket upgrade
/api/*                       → JavaScript fetch handler
GET /assets/*                → 静态文件
GET /任意前端路由             → public/index.html
其他方法                      → 405
```

SPA fallback 只能接受无扩展名的 GET 路径。不能把缺失的 `.js` 文件也返回 `index.html`，否则浏览器会把 HTML 当 JavaScript 解析。

<div class="test-result"><strong>步骤测试 4.1 · SPA/API/WS 分流</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R chatroom</code></dd>
<dt>预期输出</dt><dd><code>router sends API, WebSocket, assets, and SPA routes ... PASS</code>。</dd>
<dt>原因</dt><dd>五类路径分别命中 FETCH、WEBSOCKET、ASSET、SPA 和 NOT_FOUND，缺失脚本没有错误回退到 HTML。</dd>
</dl></div>

## 2. KJ WebSocket upgrade

在 `HttpService::request()` 中识别 `/ws` 和 `Upgrade: websocket`，然后：

```cpp
auto socket = response.acceptWebSocket(responseHeaders);
auto session = kj::heap<WebSocketSession>(
    kj::mv(socket), chatHub, log, room, user);
webSocketTasks.add(session->run().attach(kj::mv(session)));
return kj::READY_NOW;
```

`attach(session)` 保证异步循环结束前 session 不被析构。连接不再属于原 HTTP request，不能放进请求级 `ExecutionContext`。

完整关闭状态与所有权见[WebSocket 参考](../reference/websocket/)。

## 3. 内存房间

<details class="code-accordion" data-code="ch04/src/chat-hub.h">
<summary>完整文件 · ch04/src/chat-hub.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

`ChatHub` 和所有 session 都在同一个 KJ 线程使用，因此无需 mutex。不要让一个慢客户端阻塞整个房间：每个 session 维护有限发送队列，例如 64 条或 1 MiB；超过上限就以 1013 关闭该连接。

<div class="test-result"><strong>步骤测试 4.2 · 房间状态</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R chatroom</code></dd>
<dt>预期输出</dt><dd><code>ChatHub joins, broadcasts, and leaves in order ... PASS</code>。</dd>
<dt>原因</dt><dd>同房间两个 session 都收到广播；一个连接关闭后成员数从 2 降为 1。</dd>
</dl></div>

## 4. 消息协议

浏览器发送：

```json
{"type":"message","text":"hello"}
```

服务器广播：

```json
{"type":"message","room":"lobby","user":"lin","text":"hello","time":1730000000}
```

限制用户名 32 个 UTF-8 字节、消息 4 KiB、单帧 8 KiB。解析失败只关闭发送者，不影响其他连接。

## 5. 读写循环

一个 socket 不能同时启动多个写操作。`WebSocketSession` 将广播内容入队，由唯一 writer 依次发送；reader 独立接收：

```cpp
kj::Promise<void> WebSocketSession::run() {
  auto read = readLoop();
  auto write = writeLoop();
  return read.exclusiveJoin(kj::mv(write))
      .then([this] { hub.leave(room, id); });
}
```

`exclusiveJoin()` 任一侧结束就取消另一侧。清理必须幂等，因为正常 close、解析错误和客户端断网都可能进入同一条路径。

<div class="test-result"><strong>步骤测试 4.3 · WebSocket 背压</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R chatroom</code></dd>
<dt>预期输出</dt><dd><code>a slow WebSocket is closed without blocking its room ... PASS</code>。</dd>
<dt>原因</dt><dd>停止读取的连接填满 64 条队列后收到 1013；快速连接使用独立 writer queue，仍保持 open。</dd>
</dl></div>

## 6. SPA 分流

`public/index.html` 加载 `app.js`。浏览器逻辑只需要：读取 room/name、创建 `new WebSocket()`、追加消息、发送表单。

<details class="code-accordion" data-code="ch04/public/app.js">
<summary>完整文件 · ch04/public/app.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

静态文件服务必须完成三项校验：

1. URL decode 后拒绝 `..`，并确认规范化路径仍位于 `public/`。
2. 根据扩展名设置 `content-type`。
3. `index.html` 使用 `no-cache`；带内容哈希的资源可长期缓存。

## 7. 保存日志

日志独立于 stdout，写入 `data/chat.log`：

```json
{"event":"join","room":"lobby","user":"lin","connection":12,"time":1730000000}
{"event":"message","room":"lobby","user":"lin","bytes":5,"time":1730000003}
{"event":"leave","room":"lobby","user":"lin","code":1000,"time":1730000010}
```

所有调用只向 `AppendLog` 队列提交拥有所有权的字符串；单个 writer 按序追加。每秒或每 128 条 flush 一次。第四章优先保证顺序与可诊断性，第六章再测量和优化写入。

<div class="test-result"><strong>步骤测试 4.4 · 追加日志</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R chatroom</code></dd>
<dt>预期输出</dt><dd><code>chat events are appended in causal order ... PASS</code>。</dd>
<dt>原因</dt><dd>join、message、leave 从同一 KJ 线程进入单 writer，flush 后文件顺序与因果顺序一致。</dd>
</dl></div>

## 8. 验收

```bash
cmake --build runtime/build
./runtime/build/v8lab --listen 127.0.0.1:8080
```

- 打开两个浏览器窗口进入同一 room，消息双向可见。
- 访问 `/rooms/lobby` 仍返回 SPA；访问缺失的 `/assets/x.js` 返回 404。
- 关闭一个窗口后，另一个窗口收到 leave 消息。
- `data/chat.log` 依次出现 join、message、leave。
- 用慢客户端停止读取时，其他连接仍能聊天，慢连接最终被限流关闭。

<nav class="pager"><a href="../ch03/">← 第 3 章</a><a href="../ch05/">第 5 章：RocksDB KV →</a></nav>
