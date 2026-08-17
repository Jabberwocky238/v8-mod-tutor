---
title: 第 2 章 · V8 Hello World 与单元测试
description: 用最小 C++ 宿主执行 JavaScript，并建立后续章节统一使用的 KJ 测试框架
---

<p class="chapter-no">CHAPTER 02 · Hello World</p>

# Hello World 与测试底座

<p class="lead">本章只实现一个可测试的 V8 求值器。产品程序输出 Hello World；KJ test runner 验证正常返回和脚本异常。后续每个小步骤都沿用同一套测试命令与结果说明。</p>

> **本章新增：`Engine`。** 它拥有 Platform、Isolate 和 Context，并提供唯一公开方法 `evaluate(source)`。把求值从 `main()` 抽出来，是为了单元测试不必启动子进程解析 stdout。

> **本章新增：KJ test runner + CTest。** `KJ_TEST` 定义测试，`KJ_EXPECT` 验证值，`KJ_EXPECT_THROW_MESSAGE` 验证错误；CTest 负责统一发现和运行可执行测试。

## 1. 建立构建目标

`CMakeLists.txt` 定义三个目标：可复用 `v8lab_engine`、产品程序 `v8lab`、测试程序 `engine-test`。测试通过 `add_test()` 注册给 CTest。

<details class="code-accordion" data-code="ch02/CMakeLists.txt">
<summary>完整文件 · ch02/CMakeLists.txt</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
cmake -S runtime -B runtime/build -GNinja
cmake --build runtime/build --target v8lab engine-test
```

<div class="test-result"><strong>步骤测试 2.1 · 构建目标</strong><dl>
<dt>运行</dt><dd><code>cmake --build runtime/build --target engine-test</code></dd>
<dt>预期输出</dt><dd><code>[100%] Built target engine-test</code>，且退出码为 0。</dd>
<dt>原因</dt><dd>CMake 已找到 V8 monolith、KJ 和 kj-test，并完成链接；此时还没有执行测试。</dd>
</dl></div>

## 2. 实现 Engine 生命周期

头文件只暴露求值接口，隐藏 V8 handle：

<details class="code-accordion" data-code="ch02/src/engine.h">
<summary>完整文件 · ch02/src/engine.h</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

实现按 Platform → Isolate → Context 创建，按相反顺序销毁。`evaluate()` 每次建立 HandleScope 和 ContextScope，使用 `TryCatch` 将脚本异常转换成 KJ exception。

<details class="code-accordion" data-code="ch02/src/engine.c++">
<summary>完整文件 · ch02/src/engine.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

<div class="test-result"><strong>步骤测试 2.2 · 编译 Engine</strong><dl>
<dt>运行</dt><dd><code>cmake --build runtime/build --target v8lab_engine</code></dd>
<dt>预期输出</dt><dd><code>Built target v8lab_engine</code>，没有 undefined reference。</dd>
<dt>原因</dt><dd>头文件声明与实现一致，V8 编译宏和 monolith 链接参数匹配当前 V8 构建。</dd>
</dl></div>

## 3. 输出 Hello World

`main()` 只创建 Engine、求值并打印；它不复制初始化代码。

<details class="code-accordion" data-code="ch02/src/main.c++">
<summary>完整文件 · ch02/src/main.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
./runtime/build/v8lab
```

<div class="test-result"><strong>步骤测试 2.3 · Hello World</strong><dl>
<dt>预期输出</dt><dd><code>Hello, World!</code>，只占一行，退出码为 0。</dd>
<dt>原因</dt><dd>V8 计算两个字符串字面量的加法，`Utf8Value` 将结果转换为 C++ 可打印文本。</dd>
</dl></div>

## 4. 写第一组单元测试

第一个测试验证返回值；第二个测试验证脚本异常不会终止进程，而会变成带稳定前缀的 KJ exception。

<details class="code-accordion" data-code="ch02/test/engine-test.c++">
<summary>完整文件 · ch02/test/engine-test.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
ctest --test-dir runtime/build --output-on-failure
```

<div class="test-result"><strong>步骤测试 2.4 · KJ 测试</strong><dl>
<dt>预期输出</dt><dd><code>1/1 Test #1: engine ... Passed</code> 和 <code>100% tests passed</code>。</dd>
<dt>原因</dt><dd>一个测试可执行文件内的两个 `KJ_TEST` 都通过；CTest 按 executable 统计，所以显示 1 个 CTest，而不是 2 个 KJ case。</dd>
</dl></div>

## 5. 以后每一步怎么测

后续章节保持三层测试：

1. 纯 C++ 单元测试：状态机、编码、队列和限制。
2. KJ 内存 I/O 测试：timer、HTTP 和 WebSocket，不绑定真实端口。
3. 章节验收：运行完整服务，用 curl 或浏览器验证用户行为。

每个小步骤都给出预期输出和形成原因。若输出不同，先停在当前步骤修正，不要继续叠加模块。

<nav class="pager"><a href="../ch01/">← 第 1 章</a><a href="../ch03/">第 3 章：Fetch Handler →</a></nav>
