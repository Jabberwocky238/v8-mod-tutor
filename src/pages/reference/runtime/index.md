---
title: Runtime 与 cppgc 参考
description: V8 Runtime 的线程归属、handle 规则、cppgc wrapper 与关闭顺序
---

<p class="chapter-no">REFERENCE · CORE</p>

# Runtime 与 cppgc

> **职责。** Runtime 把“可以进入 JavaScript”的权力集中在一个对象里。网络、存储和 trace 模块不能直接持有 Isolate 并自行调用。

## 线程与 handle

- Runtime 在 KJ event loop 线程创建和销毁。
- `v8::Local<T>` 不离开当前 `HandleScope`，也不放入 continuation。
- 跨异步边界的 JS 值使用 `v8::Global<T>`；cppgc 对象引用 JS 值时使用并 trace `v8::TracedReference<T>`。
- 任何外部回调都通过 `runJs()` 创建 Isolate/Handle/Context scope，并在退出前执行 microtask checkpoint。

## cppgc wrapper

JS 可见宿主类型继承 `v8::Object::Wrappable`，由 `MakeGarbageCollected` 创建。C++ GC 关系使用 `cppgc::Member<T>`，并在 `Trace()` 中访问：

```cpp
void Response::Trace(cppgc::Visitor* visitor) const {
  v8::Object::Wrappable::Trace(visitor);
  visitor->Trace(bodyStream);
  visitor->Trace(cachedJsObject);
}
```

`kj::Own` 不受 cppgc 管理；不要把必须确定析构的 socket、file 或 RocksDB handle 交给 tracing GC。cppgc 适合 JS/C++ 对象图，不适合 I/O 资源所有权。

## 关闭顺序

1. 停止 accept 新 HTTP/WebSocket。
2. 拒绝新 timer、waitUntil 和 storage job。
3. 关闭 WebSocket，drain 请求和后台 TaskSet。
4. drain StorageExecutor 与 TraceWriter。
5. 清空 JS Globals 和 Context。
6. Dispose Isolate，再 Dispose V8 Platform。

析构函数不得偷偷进入 V8。需要 JS 清理时在第 3 步显式完成。

<nav class="pager"><a href="../">← 参考目录</a><a href="../request-lifecycle/">请求生命周期 →</a></nav>
