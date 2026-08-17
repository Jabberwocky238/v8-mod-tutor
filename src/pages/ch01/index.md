---
title: 第 1 章 · 安装 C++、V8 与 KJ 工具链
description: 安装一套固定版本、可校验、无需本机编译 V8 的 C++ 工具链
---

<p class="chapter-no">CHAPTER 01 · 安装</p>

# 安装开发环境

<p class="lead">这一章只准备环境，不写运行时代码。完成后我们会得到固定版本的 CMake、Ninja、Clang、libc++、V8/CppGC 和 KJ；后续章节始终复用这套工具链。</p>

> **本章新增：可复现工具链。** “我的机器能编译”不够。编译器、C++ 标准库头文件、V8 头文件和静态库必须来自同一条已知依赖链，否则常见结果不是编译错误，而是更难定位的 ABI 崩溃。

## 1. 先认识将要安装的组件

V8 负责解析和执行 JavaScript；CppGC 是随 V8 提供的 C++ 垃圾回收能力；KJ 提供 Promise、定时器、HTTP 和 WebSocket。宿主程序一直使用 C++23，不需要 Rust 编译器、Cargo 或 Rust API。

| 组件 | 固定版本 | 用途 |
|---|---:|---|
| CMake | 3.31.8 | 生成工程、运行 CTest |
| Ninja | 1.12.1 | 执行增量构建 |
| Chromium Clang | 21 init 9266 | 编译所有教程 C++ 代码 |
| V8 | 13.7.152.14 | 预编译静态归档与完全匹配的头文件 |
| Cap'n Proto / KJ | commit `a1cd1c4…` | 异步运行时与测试框架 |

> **为什么选择预编译 V8。** V8 完整构建耗时长、磁盘占用大，本教程不要求读者先成为 Chromium 构建工程师。安装脚本下载 Linux x86-64 的 pointer-compression release 归档，再配上该归档对应提交的公开 C++ 头文件。

> **为什么仍要做符号审计。** “预编译”不等于“适合 C++ 嵌入”。脚本会拒绝需要 `temporal_rs_*` ABI 的归档，也会拒绝覆盖进程 `malloc/free` 的归档。通过检查后，最终程序只使用 V8 的 C++ API。

## 2. Bootstrap 前提

机器只需预先有 `bash`、`curl`、`git`、`tar`、`gzip` 和 `sha256sum`。它们只负责下载和展开文件，不参与教程产物的 C++ 编译或链接。不要安装发行版提供的 Clang、libstdc++、KJ 或 V8 开发包。

仓库布局约定如下：

```text
/home/zq/v8v8/
├── v8-mod-tutor/    本教程
├── .deps/           固定版本的二进制工具与安装结果
└── third_party/     精确提交的头文件和第三方源码
```

## 3. 执行完整安装脚本

完整脚本独立保存，网页中的内容直接从该文件加载；展开后可以逐行核对，右上角按钮可复制全文。

<details class="code-accordion" data-code="ch01/install-deps.sh">
<summary>完整文件 · ch01/install-deps.sh</summary>
<div class="code-shell"><button class="copy-code" type="button" aria-label="复制完整代码"></button><pre><code>正在加载源码...</code></pre></div>
</details>

```bash
export LAB_ROOT=/home/zq/v8v8
bash "$LAB_ROOT/v8-mod-tutor/src/code/ch01/install-deps.sh"
```

脚本按依赖顺序完成四件事：先校验并展开 CMake、Ninja 和 Clang；再下载预编译 V8 归档并审计符号；随后取回完全匹配的 V8/libc++ 头文件；最后用这套工具链编译 KJ 并运行它自己的测试。

<div class="test-result"><strong>步骤测试 1.1 · 下载完整性</strong><dl>
<dt>运行</dt><dd>安装脚本中的每一次 <code>sha256sum --check --status</code>。</dd>
<dt>预期输出</dt><dd>校验成功时不打印内容并继续；文件损坏时脚本立即退出且返回非 0。</dd>
<dt>原因</dt><dd>静默成功便于看清真正的构建日志；SHA256 将本地文件与教程锁定的发布产物一一对应。</dd>
</dl></div>

<div class="test-result"><strong>步骤测试 1.2 · V8 原生符号</strong><dl>
<dt>运行</dt><dd>脚本使用 <code>llvm-nm</code> 检查未解析符号和全局分配器符号。</dd>
<dt>预期输出</dt><dd>不出现 <code>temporal_rs_</code>，也不出现归档定义的 <code>malloc</code> 或 <code>free</code>。</dd>
<dt>原因</dt><dd>这证明该归档能作为普通 C++ V8 库链接，不需要补入 Rust ABI，也不会接管整个宿主进程的分配器。</dd>
</dl></div>

<div class="test-result"><strong>步骤测试 1.3 · KJ 自测</strong><dl>
<dt>运行</dt><dd><code>ctest --test-dir "$LAB_ROOT/third_party/capnproto/build-v137" --output-on-failure</code></dd>
<dt>预期输出</dt><dd><code>100% tests passed, 0 tests failed out of 5</code>。</dd>
<dt>原因</dt><dd>五个 CTest 组覆盖 KJ、KJ async 与 Cap'n Proto；全部通过后才将静态库安装到 <code>.deps/v137</code>。</dd>
</dl></div>

## 4. 最终验收

安装末尾应出现类似下面四行：

```text
V8_CPP:OK
KJ:OK
clang version 21.0.0 (...)
1.12.1
```

`V8_CPP:OK` 表示归档存在且通过原生符号检查；`KJ:OK` 表示固定提交已经用同一编译器和 libc++ 构建、测试并安装。版本行则防止后续命令悄悄调用系统工具。

至此不需要编译 V8，也没有安装或使用 Rust。下一章从最小 Hello World 开始，同时搭好以后每个步骤都要使用的单元测试框架。

<nav class="pager"><a href="../">← 课程首页</a><a href="../ch02/">第 2 章：Hello World →</a></nav>
