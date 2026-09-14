let ready = null
let lines = []

function loadScript(src) {
  return new Promise((resolve, reject) => {
    if (document.querySelector(`script[data-aul="1"]`)) return resolve()
    const s = document.createElement('script')
    s.src = src
    s.dataset.aul = '1'
    s.onload = resolve
    s.onerror = () => reject(new Error('could not load ' + src))
    document.body.appendChild(s)
  })
}

export function ensureAul(base) {
  if (ready) return ready
  ready = (async () => {
    window.Module = {
      print: (t) => lines.push(t),
      printErr: (t) => lines.push(t),
    }
    const initialized = new Promise((resolve, reject) => {
      window.Module.onRuntimeInitialized = () => {
        try {
          window.Module.ccall('aul_init')
          resolve()
        } catch (e) {
          reject(e)
        }
      }
      setTimeout(() => reject(new Error('wasm init timed out')), 15000)
    })
    await loadScript(base + 'aul.js')
    await initialized
    return {
      run(code) {
        lines = []
        const status = window.Module.ccall('aul_run', 'number', ['string'], [code])
        return { status, output: lines.join('\n') }
      },
      disasm(code) {
        lines = []
        const status = window.Module.ccall('aul_disasm', 'number', ['string'], [code])
        return { status, output: lines.join('\n') }
      },
      writeFile(path, content) {
        window.Module.FS.writeFile(path, content)
      },
    }
  })()
  ready.catch(() => { ready = null })
  return ready
}
