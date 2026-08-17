---
title: WebSocket 与 SPA 参考
description: KJ WebSocket session、背压、关闭和静态资源路由边界
---

<p class="chapter-no">REFERENCE · REALTIME</p>

# WebSocket 与 SPA

> **生命周期边界。** Upgrade 完成后，WebSocket session 已脱离原 HTTP request。它属于进程级连接集合，不属于 ExecutionContext，也不应被 request cancellation 销毁。

## Session 不变量

- 一个 reader、一个串行 writer。
- 发送队列按消息数和字节数双重限额。
- join 成功后一定对应一次 leave，即使关闭握手失败。
- 广播遍历稳定 connection ID，不在遍历中直接删除 vector 元素。
- 服务退出发送 1001；协议错误发送 1002；消息过大发送 1009；过载发送 1013。

## SPA 安全

URL path 解码、规范化后必须仍位于 public root。只有没有文件扩展名且接受 `text/html` 的 GET 才进入 SPA fallback。静态资源缺失直接 404。

## 日志

聊天日志不保存消息正文，只保留 room、user、字节数、连接 ID、事件和时间。若确需正文，必须另行定义保留周期、访问控制和脱敏策略。

<nav class="pager"><a href="../request-lifecycle/">← 请求生命周期</a><a href="../storage/">存储 →</a></nav>
