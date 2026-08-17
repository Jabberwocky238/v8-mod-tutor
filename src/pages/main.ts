import "$/styles/global.css"
import hljs from "highlight.js/lib/core"
import bash from "highlight.js/lib/languages/bash"
import cmake from "highlight.js/lib/languages/cmake"
import cpp from "highlight.js/lib/languages/cpp"
import javascript from "highlight.js/lib/languages/javascript"
import json from "highlight.js/lib/languages/json"
import { Check, Copy, createIcons } from "lucide"

hljs.registerLanguage("bash", bash)
hljs.registerLanguage("cmake", cmake)
hljs.registerLanguage("cpp", cpp)
hljs.registerLanguage("javascript", javascript)
hljs.registerLanguage("json", json)

const files = import.meta.glob("../code/**/*", {
  eager: true,
  import: "default",
  query: "?raw",
}) as Record<string, string>

const byName = new Map(
  Object.entries(files).map(([path, source]) => [
    path.replace(/^\.\.\/code\//, ""),
    source,
  ]),
)

const languageFor = (path: string): string => {
  if (/CMakeLists\.txt$|\.cmake$/.test(path)) return "cmake"
  if (/\.(c\+\+|cc|cpp|h)$/.test(path)) return "cpp"
  if (/\.js$/.test(path)) return "javascript"
  if (/\.json$/.test(path)) return "json"
  return "bash"
}

for (const details of document.querySelectorAll<HTMLDetailsElement>(
  "details.code-accordion[data-code]",
)) {
  const path = details.dataset.code ?? ""
  const source = byName.get(path)
  const code = details.querySelector<HTMLElement>("pre code")
  const button = details.querySelector<HTMLButtonElement>("button.copy-code")

  if (!source || !code || !button) {
    details.classList.add("code-error")
    if (code) code.textContent = `Source not found: ${path}`
    continue
  }

  const setCopyState = (copied: boolean) => {
    button.innerHTML = `<i data-lucide="${copied ? "check" : "copy"}"></i>`
    button.setAttribute("aria-label", copied ? "已复制" : "复制完整代码")
    createIcons({ icons: { Check, Copy } })
  }

  code.innerHTML = hljs.highlight(source, { language: languageFor(path) }).value
  setCopyState(false)
  button.addEventListener("click", async () => {
    if (navigator.clipboard) {
      await navigator.clipboard.writeText(source)
    } else {
      const input = document.createElement("textarea")
      input.value = source
      input.style.position = "fixed"
      input.style.opacity = "0"
      document.body.append(input)
      input.select()
      document.execCommand("copy")
      input.remove()
    }
    setCopyState(true)
    window.setTimeout(() => {
      setCopyState(false)
    }, 1400)
  })
}
