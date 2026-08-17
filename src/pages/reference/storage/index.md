---
title: RocksDB KV 参考
description: KV key schema、工作线程边界、持久性选项和关闭流程
---

<p class="chapter-no">REFERENCE · DATA</p>

# RocksDB KV

> **隔离边界。** RocksDB 是同步库，StorageExecutor 是唯一异步适配层。binding 负责 JS 类型转换，KvStore 负责数据库语义，二者不要互相渗透。

## Key schema

```text
physical key = namespace bytes + 0x00 + user key bytes
```

前缀 list 的 iterator upper bound 应限制在 namespace 范围内。cursor 编码最后一个 physical key，并做 base64url；解码后再次校验 namespace。

## 错误映射

| RocksDB / host | JavaScript |
| --- | --- |
| NotFound | `null` |
| InvalidArgument | `TypeError` |
| queue full | `StorageBusyError` |
| IO/Corruption | `StorageError`，记录内部详情 |

不要把本地绝对路径或 RocksDB 内部错误全文返回给远程客户端。

## 关闭

停止入队，完成已接受 job，flush WAL，根据配置执行 `SyncWAL()`，销毁 column-family handle，最后销毁 DB。TraceWriter 不应依赖已经销毁的 KvStore。

<nav class="pager"><a href="../websocket/">← WebSocket</a><a href="../trace/">Trace →</a></nav>
