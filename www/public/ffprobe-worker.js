const ffprobeModulePromise = new Promise((resolve, reject) => {
  try {
    self.importScripts('ffprobe-wasm.js')
    Module.onRuntimeInitialized = () => resolve(Module)
  } catch (error) {
    console.log('ffprobe failed to load "ffprobe-wasm.js"')
    reject(new Error("Failed to load Wasm module"))
  }
})

onmessage = async (message) => {
  const [type, file] = message.data
  const [origin] = message.ports

  switch (type) {
  case 'get_file_info': {
    // Mount FS for files.
    if (!FS.analyzePath('/work').exists) {
      FS.mkdir('/work')
    }
    FS.mount(WORKERFS, { files: [file] }, '/work')

    try {
      // wait for module to be ready
      const ffprobeModule = await ffprobeModulePromise
      // Call the wasm module.
      const { name, error, ...info } = ffprobeModule.get_file_info(`/work/${file.name}`)

      // Remap streams into collection.
      const streams = []
      for (let i = 0; i < info.streams.size(); i++) {
        const tags = {}
        for (let j = 0; j < info.streams.get(i).tags.size(); j++) {
          const t = info.streams.get(i).tags.get(j)
          tags[t.key] = t.value
        }
        streams.push({ ...info.streams.get(i), tags })
      }

      // Remap chapters into collection.
      const chapters = []
      for (let i = 0; i < info.chapters.size(); i++) {
        const tags = {}
        for (let j = 0; j < info.chapters.get(i).tags.size(); j++) {
          const t = info.chapters.get(i).tags.get(j)
          tags[t.key] = t.value
        }
        chapters.push({ ...info.chapters.get(i), ...{ tags } })
      }

      // Send back data response.

      origin.postMessage({
        error,
        streams,
        chapters,
        format: {
          ...info,
          format_name: name,
        },
      })
    } catch (unknownError) {
      origin.postMessage({ error: `ffprobe crashed: ${unknownError.stack}` })
    }

    // Cleanup mount.
    FS.unmount('/work')
    break
  }

  default:
    break
  }
}
