import "$/styles/global.css"
import hljs from "highlight.js/lib/core"
import bash from "highlight.js/lib/languages/bash"
import cmake from "highlight.js/lib/languages/cmake"
import cpp from "highlight.js/lib/languages/cpp"
import css from "highlight.js/lib/languages/css"
import javascript from "highlight.js/lib/languages/javascript"
import json from "highlight.js/lib/languages/json"
import xml from "highlight.js/lib/languages/xml"
import { Check, Copy, createIcons } from "lucide"

type Theme = "light" | "dark" | "system"

const themeButtons = document.querySelectorAll<HTMLButtonElement>(
  ".theme-switcher [data-theme-value]",
)

const setTheme = (theme: Theme) => {
  document.documentElement.dataset.theme = theme
  localStorage.setItem("v8-mod-theme", theme)
  for (const button of themeButtons) {
    const selected = button.dataset.themeValue === theme
    button.setAttribute("aria-pressed", String(selected))
  }
}

for (const button of themeButtons) {
  button.addEventListener("click", () => setTheme(button.dataset.themeValue as Theme))
}

const initialTheme = document.documentElement.dataset.theme as Theme | undefined
setTheme(initialTheme ?? "system")

hljs.registerLanguage("bash", bash)
hljs.registerLanguage("cmake", cmake)
hljs.registerLanguage("cpp", cpp)
hljs.registerLanguage("css", css)
hljs.registerLanguage("javascript", javascript)
hljs.registerLanguage("json", json)
hljs.registerLanguage("xml", xml)

const files = import.meta.glob(["../code/**/*", "!../code/**/build-*/**"], {
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
  if (/\.css$/.test(path)) return "css"
  if (/\.html$/.test(path)) return "xml"
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
