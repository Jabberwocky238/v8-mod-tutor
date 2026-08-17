---
title: 第 2 章 · V8 Hello World 与单元测试
description: 用最小 C++ 宿主执行 JavaScript，并建立后续章节统一使用的 KJ 测试框架
---

<p class="chapter-no">CHAPTER 02 · Hello World</p>

# Hello World 与测试底座

<p class="lead">本章只实现一个可测试的 V8 求值器。产品程序输出 Hello World；KJ test runner 验证正常返回和脚本异常。后续每个小步骤都沿用同一套测试命令与结果说明。</p>

> **本章新增：`Engine`。** 它拥有 Platform、Isolate 和 Context，并提供唯一公开方法 `evaluate(source)`。把求值从 `main()` 抽出来，是为了单元测试不必启动子进程解析 stdout。

> **本章新增：KJ test runner + CTest。** `KJ_TEST` 定义测试，`KJ_EXPECT` 验证值，`KJ_EXPECT_THROW_MESSAGE` 验证错误；CTest 负责统一发现和运行可执行测试。

> **本章新增：锁定工具链文件。** CMake 通过 `v8-pinned.cmake` 选择第一章安装的 Clang、libc++、Ninja 和 V8 静态归档。后续命令不会根据当前 shell 的 `PATH` 猜测编译器。

## 1. 建立构建目标

`CMakeLists.txt` 定义三个目标：可复用 `v8lab_engine`、产品程序 `v8lab`、测试程序 `engine-test`。测试通过 `add_test()` 注册给 CTest。

<details class="code-accordion" data-code="ch02/CMakeLists.txt">
<summary>完整文件 · ch02/CMakeLists.txt</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
CMAKE="$LAB_ROOT/.deps/cmake-3.31.8/bin/cmake"
SOURCE="$LAB_ROOT/v8-mod-tutor/src/code/ch02"
BUILD="$SOURCE/build-v137"

"$CMAKE" -S "$SOURCE" -B "$BUILD" -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$LAB_ROOT/v8-mod-tutor/toolchain/v8-pinned.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$LAB_ROOT/.deps/v137"
"$CMAKE" --build "$BUILD" --target v8lab engine-test
```

<div class="test-result"><strong>步骤测试 2.1 · 构建目标</strong><dl>
<dt>运行</dt><dd><code>"$CMAKE" --build "$BUILD" --target engine-test</code></dd>
<dt>预期输出</dt><dd>最后出现 <code>Linking CXX executable engine-test</code>，且退出码为 0。</dd>
<dt>原因</dt><dd>Ninja 已使用锁定的 Clang 编译 Engine 和测试，并链接预编译 V8、KJ 与 kj-test；此时还没有执行测试。</dd>
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
<dt>运行</dt><dd><code>"$CMAKE" --build "$BUILD" --target v8lab_engine</code></dd>
<dt>预期输出</dt><dd>首次构建出现 <code>Linking CXX static library libv8lab_engine.a</code>；重复构建显示 <code>ninja: no work to do.</code>。</dd>
<dt>原因</dt><dd>第一次输出证明 Engine 编译完成；第二种输出表示输入未改变，Ninja 正确复用了结果。静态库阶段不会产生最终 V8 链接。</dd>
</dl></div>

## 3. 输出 Hello World

`main()` 只创建 Engine、求值并打印；它不复制初始化代码。

<details class="code-accordion" data-code="ch02/src/main.c++">
<summary>完整文件 · ch02/src/main.c++</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
"$BUILD/v8lab"
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
"$BUILD/engine-test"
"$LAB_ROOT/.deps/cmake-3.31.8/bin/ctest" \
  --test-dir "$BUILD" --output-on-failure
```

<div class="test-result"><strong>步骤测试 2.4 · KJ 测试</strong><dl>
<dt>预期输出</dt><dd>KJ 先分别打印两个 <code>[PASS]</code>，末行是 <code>2 test(s) passed</code>；CTest 随后打印 <code>1/1 Test #1: engine ... Passed</code> 和 <code>100% tests passed</code>。</dd>
<dt>原因</dt><dd>正常求值和异常转换两个 <code>KJ_TEST</code> 都通过；CTest 按测试可执行文件统计，所以它显示 1 个 CTest，而不是 2 个 KJ case。</dd>
</dl></div>

## 5. 以后每一步怎么测

后续章节保持三层测试：

1. 纯 C++ 单元测试：状态机、编码、队列和限制。
2. KJ 内存 I/O 测试：timer、HTTP 和 WebSocket，不绑定真实端口。
3. 章节验收：运行完整服务，用 curl 或浏览器验证用户行为。

每个小步骤都给出预期输出和形成原因。若输出不同，先停在当前步骤修正，不要继续叠加模块。

<nav class="pager"><a href="../ch01/">← 第 1 章</a><a href="../ch03/">第 3 章：Fetch Handler →</a></nav>
