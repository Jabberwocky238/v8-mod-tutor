---
title: V8 Runtime Lab · 用 C++、KJ 与 cppgc 构建运行时
description: 从 V8 Hello World 逐步构建 Fetch、WebSocket Chatroom、RocksDB KV 与性能追踪模块
---

<div class="hero">
  <p class="eyebrow">C++ · KJ · CPPGC</p>
  <h1>从 V8 到一个小型运行时</h1>
  <p class="lead">不是复刻完整平台。我们只保留一条清晰主线：让 V8 执行 JavaScript，用 KJ 接管异步 I/O，用 cppgc 管理跨语言对象，然后逐章加入真实能力。</p>
  <div class="hero-actions"><a class="primary" href="./ch01/">从安装开始</a><a href="./reference/">先看模块地图</a></div>
</div>

<div class="facts">
  <div><strong>6</strong><span>个连续章节</span></div>
  <div><strong>C++23</strong><span>宿主实现</span></div>
  <div><strong>1</strong><span>个逐步长大的服务</span></div>
</div>

## 最终得到什么

```js
export default {
  async fetch(request, env, ctx) {
    const value = await env.KV.get("visits")
    ctx.waitUntil(env.LOG.append("request completed"))
    await new Promise(resolve => setTimeout(resolve, 3000))
    return new Response(value ?? "hello")
  }
}
```

同一个进程还会提供 SPA 静态资源、WebSocket 聊天室、内存房间状态、追加日志、RocksDB KV，以及请求和存储 span。

## 路线

<div class="chapter-grid">
  <a href="./ch01/"><span>01 · 安装</span><strong>准备 C++ 构建环境</strong><p>V8、KJ、cppgc 与调试工具。</p></a>
  <a href="./ch02/"><span>02 · 起点</span><strong>Hello World</strong><p>Isolate、Context、微任务与统一 C++ heap。</p></a>
  <a href="./ch03/"><span>03 · HTTP</span><strong>Fetch Handler</strong><p>Request/Response、setTimeout、ExecutionContext。</p></a>
  <a href="./ch04/"><span>04 · 实时</span><strong>Chatroom</strong><p>WebSocket、SPA 分流、内存状态与日志。</p></a>
  <a href="./ch05/"><span>05 · 持久化</span><strong>RocksDB KV</strong><p>异步存储边界、批量写入与恢复。</p></a>
  <a href="./ch06/"><span>06 · 观测</span><strong>Trace 与压测</strong><p>找出写入瓶颈并用数据优化。</p></a>
</div>

> 每章都只在上一章的可运行程序上增加一个边界。章节正文解释“为什么”，完整类型职责放在独立参考页，避免把生命周期细节藏在代码片段里。

[进入第 1 章：安装 →](./ch01/)
