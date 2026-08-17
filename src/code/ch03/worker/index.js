export default {
  async fetch(request, env, ctx) {
    await new Promise((resolve) => setTimeout(resolve, 3000))

    ctx.waitUntil(
      new Promise((resolve) => {
        setTimeout(() => {
          console.log(`finished background work for ${request.url}`)
          resolve()
        }, 250)
      }),
    )

    return new Response("hello after 3 seconds", {
      status: 200,
      headers: { "content-type": "text/plain; charset=utf-8" },
    })
  },
}
