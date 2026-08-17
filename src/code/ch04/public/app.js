const form = document.querySelector("form")
const input = document.querySelector("input")
const messages = document.querySelector("ol")
const params = new URLSearchParams(location.search)
const room = params.get("room") || "lobby"
const name = params.get("name") || "guest"
const scheme = location.protocol === "https:" ? "wss" : "ws"
const socket = new WebSocket(
  `${scheme}://${location.host}/ws?room=${encodeURIComponent(room)}&name=${encodeURIComponent(name)}`,
)

socket.addEventListener("message", (event) => {
  const message = JSON.parse(event.data)
  const item = document.createElement("li")
  item.textContent = `${message.user ?? "system"}: ${message.text ?? message.type}`
  messages.append(item)
})

form.addEventListener("submit", (event) => {
  event.preventDefault()
  if (socket.readyState !== WebSocket.OPEN || !input.value) return
  socket.send(JSON.stringify({ type: "message", text: input.value }))
  input.value = ""
})
