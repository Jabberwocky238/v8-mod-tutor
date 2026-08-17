const address = process.argv[2] ?? "ws://127.0.0.1:8080/ws"

const connect = (name) =>
  new Promise((resolve, reject) => {
    const socket = new WebSocket(`${address}?room=lobby&name=${name}`)
    socket.addEventListener("open", () => resolve(socket), { once: true })
    socket.addEventListener("error", reject, { once: true })
  })

const nextMessage = (socket) =>
  new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error("message timeout")), 2000)
    socket.addEventListener(
      "message",
      (event) => {
        clearTimeout(timeout)
        resolve(JSON.parse(event.data))
      },
      { once: true },
    )
  })

const alice = await connect("alice")
const bob = await connect("bob")
const aliceMessage = nextMessage(alice)
const bobMessage = nextMessage(bob)
alice.send("hello")

const received = await Promise.all([aliceMessage, bobMessage])
if (received.some((message) => message.user !== "alice" || message.text !== "hello")) {
  throw new Error(`unexpected broadcast: ${JSON.stringify(received)}`)
}

alice.close(1000)
bob.close(1000)
console.log("2 clients received alice: hello")
