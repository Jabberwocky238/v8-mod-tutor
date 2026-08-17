globalThis.worker = {
  async fetch(request, env) {
    await env.KV.put("room:lobby:topic", "V8 internals")
    const topic = await env.KV.get("room:lobby:topic")
    const page = await env.KV.list("room:", 100)
    return new Response(`${topic}; keys=${page.keys.length}`)
  },
}
