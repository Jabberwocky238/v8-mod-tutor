---
title: Trace 模块参考
description: Span schema、上下文传播、采样、有界写入和性能指标
---

<p class="chapter-no">REFERENCE · OBSERVE</p>

# Trace 模块

> **Trace 不能成为故障源。** 采集路径禁止阻塞业务、禁止无界分配、禁止抛异常。容量不足时丢 trace 并增加 dropped counter。

## 最小字段

`traceId`、`spanId`、`parentSpanId`、`name`、单调开始/持续时间、状态和有限属性。墙钟只用于展示，耗时一律使用 `kj::MonotonicClock`。

## 基数控制

路由记录 `/rooms/:room`，不记录实际 room 名；KV 记录 key 长度和 operation，不记录 key/value；错误记录稳定类别，不把完整 stack 当标签。

## 采样

在根 trace 创建时决定采样，子 span 继承。错误可以提升为保留，但必须限制每秒最大错误 trace，避免故障时日志风暴。

## 核心指标

- HTTP requests、errors、active requests、p50/p95/p99。
- WebSocket active、queue bytes、slow-client closes。
- Storage queue depth/time、operation time、batch size、errors。
- Trace queue depth、written、dropped、flush time。

<nav class="pager"><a href="../storage/">← 存储</a><a href="../../ch06/">回到第 6 章 →</a></nav>
