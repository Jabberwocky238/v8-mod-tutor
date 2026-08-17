globalThis.worker = {
  async fetch(request, env, ctx) {
    await new Promise((resolve) => setTimeout(resolve, 3000))

    ctx.waitUntil(
      new Promise((resolve) => {
        setTimeout(resolve, 250)
      }),
    )

    return new Response(`hello after 3 seconds: ${request.url}`, {
      status: 200,
    })
  },
}
