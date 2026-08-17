# V8 Runtime Lab

中文教程站点：使用 C++23、V8 Embedder API、KJ 与 cppgc，逐章构建 Fetch Runtime、WebSocket Chatroom、RocksDB KV 和 Trace 模块。

完整示例源码位于 `src/code/`，Markdown 使用代码手风琴引用这些文件。

```bash
bun install --frozen-lockfile
bun run dev
```

生产构建：`bun run build`，输出到 `dist/`。

Cloudflare 静态资源构建校验：

```bash
bun run cf:build
```

校验通过后，使用 `bun run cf:deploy` 发布。该命令需要 Cloudflare 登录或 API Token。
