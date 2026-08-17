---
title: Fetch 请求生命周期参考
description: Fetch Promise、setTimeout、ExecutionContext waitUntil 与取消传播
---

<p class="chapter-no">REFERENCE · HTTP</p>

# 请求生命周期

> **两个完成点。** response completion 决定客户端何时收到完整响应；lifetime completion 决定请求相关后台工作何时全部结束。`waitUntil()` 只影响后者。

## 状态机

```text
ACCEPTED → JS_RUNNING → RESPONSE_PENDING → RESPONSE_SENT → DRAINING → DONE
              │               │                 │
              └ error → 500   └ disconnect      └ waitUntil timeout
```

- fetch resolve 非 Response：500。
- fetch reject：记录 JS stack，若尚未发 headers 则 500。
- 客户端在响应前断开：取消 body 和 fetch 等待；是否继续 waitUntil 由策略决定并保持一致。
- waitUntil reject：不改已发送响应，只写日志和 trace。

## Timer

Timer entry 拥有 callback 的 `TracedReference`、ID、到期时间和 KJ promise。`clearTimeout()` 必须同时取消 KJ task 和释放 callback。回调运行期间调用 clear 时，清理应幂等。

## 限额

建议从小值开始：单请求 128 个活跃 timer、30 秒 waitUntil 上限、1 MiB request body、8 MiB response body。超限必须成为可见 JS 异常，不能静默丢任务。

<nav class="pager"><a href="../runtime/">← Runtime</a><a href="../websocket/">WebSocket →</a></nav>
