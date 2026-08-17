export default {
  async fetch(request, env) {
    const url = new URL(request.url)
    if (url.pathname !== "/api/topic") {
      return new Response("not found", { status: 404 })
    }

    const key = `room:${url.searchParams.get("room") || "lobby"}:topic`
    if (request.method === "PUT") {
      await env.KV.put(key, await request.text())
      return new Response(null, { status: 204 })
    }
    if (request.method === "GET") {
      const topic = await env.KV.get(key)
      return topic === null
        ? new Response("not found", { status: 404 })
        : new Response(topic)
    }
    return new Response("method not allowed", { status: 405 })
  },
}
