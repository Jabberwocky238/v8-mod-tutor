---
title: 第 5 章 · 用 RocksDB 实现 env.KV
description: 将 RocksDB 接入 KJ/V8 运行时，提供异步 get、put、delete 与 list
---

<p class="chapter-no">CHAPTER 05 · Storage</p>

# 接入 RocksDB，做一个 KV 数据库

<p class="lead">聊天室仍保留内存状态。本章新增通用持久化绑定 <code>env.KV</code>，让 worker 可以保存昵称、房间公告和计数器，而不会在 V8 线程同步等待磁盘。</p>

> **本章新增：`KvStore`。** 它唯一拥有 RocksDB 实例，负责 key 编码、大小限制、column family 与错误翻译。V8 binding 不直接包含 RocksDB 头文件。

> **本章新增：`StorageExecutor`。** 固定大小工作线程池执行阻塞数据库调用。输入和输出都是拥有所有权的 `kj::String` / `kj::Array<byte>`，不跨线程携带 V8 handle。

> **本章新增：`KvNamespace`。** 由 cppgc 管理的 JS wrapper，暴露 `get()`、`put()`、`delete()`、`list()`，每个方法返回 JavaScript Promise。

<details class="code-accordion" data-code="ch05/test/storage-test.c++">
<summary>完整测试 · ch05/test/storage-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

## 1. 安装与链接

Ubuntu / Debian：

```bash
sudo apt install -y librocksdb-dev
pkg-config --modversion rocksdb
```

在 CMake 中使用 `pkg_check_modules(ROCKSDB REQUIRED rocksdb)`，并把 include、library 和 compiler flags 只挂到 storage target。

## 2. 数据模型

打开 `data/kv`，创建两个 column family：

| Column family | 内容 |
| --- | --- |
| `default` | 元数据与 schema version |
| `kv` | 用户 key/value |

实际 key 编码为 `<namespace>\0<user-key>`。这样以后可以为不同 worker 创建逻辑 namespace，而不需要为每个 namespace 建 column family。

限制必须在排队前检查：key 不超过 1 KiB，value 不超过 1 MiB，list limit 为 1–1000。拒绝含 NUL 的 namespace，避免前缀边界模糊。

<div class="test-result"><strong>步骤测试 5.1 · Key 编码</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R storage</code></dd>
<dt>预期输出</dt><dd><code>physical keys isolate namespaces ... PASS</code>。</dd>
<dt>原因</dt><dd>相同用户 key 在不同 namespace 下生成不同物理 key，NUL 分隔符提供明确前缀边界。</dd>
</dl></div>

## 3. C++ 存储接口

<details class="code-accordion" data-code="ch05/src/kv-store.h">
<summary>完整文件 · ch05/src/kv-store.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

`KvStore` 方法是同步的，因为 RocksDB API 本身同步；异步边界放在调用者 `StorageExecutor`，而不是伪装成内部 Promise。

<div class="test-result"><strong>步骤测试 5.2 · KV 基本语义</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R storage</code></dd>
<dt>预期输出</dt><dd><code>KvStore supports put, get, delete, and prefix list ... PASS</code>。</dd>
<dt>原因</dt><dd>两个前缀 key 可被同一 snapshot iterator 找到，删除后 Get 正确映射为 `kj::none`。</dd>
</dl></div>

## 4. 工作线程与 KJ 回传

```text
V8/KJ thread                          storage worker
-------------                        --------------
env.KV.get("x")
  创建 JS Promise
  复制 namespace/key  ─────────────→ RocksDB::Get
                                      复制结果
  KJ event port 唤醒 ←─────────────  完成消息入队
  进入 isolate
  resolve/reject
  microtask checkpoint
```

> **硬规则：V8 handle 不跨线程。** `v8::Local`、`v8::Global`、Context、Isolate 和 cppgc 对象都不进入 storage worker。worker 只处理字节；resolve Promise 的动作永远回到 Runtime 线程。

使用有界队列，例如 1024 个操作。队列满时立即 reject 为 `StorageBusyError`，而不是无限积压并耗尽内存。

<div class="test-result"><strong>步骤测试 5.3 · 工作队列压力</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R storage</code></dd>
<dt>预期输出</dt><dd><code>StorageExecutor reports saturation ... PASS</code>。</dd>
<dt>原因</dt><dd>容量为 2 的测试队列已被占满，第三项立即得到 StorageBusyError，没有增长为第三个 pending job。</dd>
</dl></div>

## 5. JS API

```js
await env.KV.put("room:lobby:topic", "V8 internals")
const topic = await env.KV.get("room:lobby:topic")
await env.KV.delete("room:lobby:topic")

const page = await env.KV.list({ prefix: "room:lobby:", limit: 100 })
// { keys: ["room:lobby:topic"], cursor: null, complete: true }
```

本教程默认 UTF-8 字符串值。binding 内部仍以 bytes 保存，为以后增加 `arrayBuffer` 类型留下空间。

<div class="test-result"><strong>步骤测试 5.4 · JS Promise 回传</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R storage</code></dd>
<dt>预期输出</dt><dd><code>env.KV resolves on the runtime thread ... PASS</code>。</dd>
<dt>原因</dt><dd>RocksDB worker 只返回 bytes；KJ event port 唤醒 Runtime 线程后才 resolve JS Promise。</dd>
</dl></div>

## 6. cppgc wrapper

`KvNamespace` 不拥有数据库，只保存生命周期受 Runtime 保证的 executor 指针与 namespace：

```cpp
class KvNamespace final : public v8::Object::Wrappable {
 public:
  KvNamespace(StorageExecutor& executor, kj::String name)
      : executor(executor), name(kj::mv(name)) {}

  void Trace(cppgc::Visitor* visitor) const override {
    v8::Object::Wrappable::Trace(visitor);
  }

 private:
  StorageExecutor& executor;
  kj::String name;
};
```

Runtime 关闭时先拒绝新请求，再 drain 或取消 storage queue，最后才能销毁 RocksDB、cppgc heap 和 Isolate。

## 7. 在聊天室中使用

保留 `ChatHub` 内存模型，只将房间 topic 持久化：

<details class="code-accordion" data-code="ch05/worker/index.js">
<summary>完整文件 · ch05/worker/index.js</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

## 8. 一致性与崩溃语义

- 单个 `put`/`delete` 是原子的。
- 本章默认 WAL 开启；Promise fulfilled 表示写入已进入 RocksDB WAL，不等于磁盘介质已 `fsync`。
- 需要强持久性时为该写入设置 `sync=true`，代价留到第六章测量。
- `list` 使用 RocksDB snapshot，保证同一页迭代期间视图稳定；跨页不承诺事务快照。

完整选项与 key schema 见[存储参考](../reference/storage/)。

<div class="test-result"><strong>步骤测试 5.5 · 重启恢复</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir runtime/build -V -R storage</code></dd>
<dt>预期输出</dt><dd><code>RocksDB data survives a clean reopen ... PASS</code>。</dd>
<dt>原因</dt><dd>第一个 DB 实例关闭后，第二个实例从同一临时目录读取 WAL/SST，得到先前写入的 `saved`。</dd>
</dl></div>

## 9. 验收

1. `put → get → delete → get` 返回预期值。
2. 重启进程后 topic 仍存在。
3. 并发写同一 key 不崩溃，最后值属于某个已成功操作。
4. 非法大 key/value 在 V8 线程立即 reject，不进入工作队列。
5. 压满存储队列时返回明确错误，HTTP 服务与 WebSocket 心跳仍响应。

<nav class="pager"><a href="../ch04/">← 第 4 章</a><a href="../ch06/">第 6 章：Trace 与优化 →</a></nav>
