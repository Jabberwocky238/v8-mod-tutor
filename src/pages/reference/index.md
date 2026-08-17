---
title: 模块参考 · V8 Runtime Lab
description: 运行时、请求、WebSocket、存储与 Trace 的职责和生命周期参考
---

<p class="chapter-no">REFERENCE</p>

# 模块参考

<p class="lead">章节负责带你完成改动；这里负责把边界说完整。遇到悬空引用、取消传播、跨线程或关闭顺序问题时，从对应模块开始查。</p>

<div class="chapter-grid">
  <a href="./runtime/"><span>CORE</span><strong>Runtime 与 cppgc</strong><p>初始化、线程归属、wrapper 和关闭顺序。</p></a>
  <a href="./request-lifecycle/"><span>HTTP</span><strong>请求生命周期</strong><p>fetch Promise、timer、waitUntil 与取消。</p></a>
  <a href="./websocket/"><span>REALTIME</span><strong>WebSocket</strong><p>upgrade、session、背压和 SPA 分流。</p></a>
  <a href="./storage/"><span>DATA</span><strong>RocksDB KV</strong><p>线程边界、key schema、写入与恢复。</p></a>
  <a href="./trace/"><span>OBSERVE</span><strong>Trace</strong><p>span、传播、采样和有界 writer。</p></a>
</div>

## 总体所有权

```text
Process
├── KJ AsyncIoContext
├── Runtime
│   ├── V8 Platform / Isolate / Context
│   ├── CppHeap
│   ├── TimerQueue
│   └── request/background TaskSet
├── HttpServer → RouterService
├── ChatHub → WebSocketSession...
├── StorageExecutor → RocksDB
└── TraceWriter
```

> 最重要的不变量：只有 Runtime 所在线程可以进入 V8；KJ continuation 可以携带任务状态，不能携带失效的 `v8::Local`；后台线程只处理拥有所有权的普通 C++ 数据。

<nav class="pager"><a href="../">← 课程首页</a><a href="./runtime/">Runtime 参考 →</a></nav>
