import { useState, useEffect } from 'react'
import AulEditor from './AulEditor.jsx'
import { ensureAul } from '../wasm/aulWasm.js'
import { aulFiles } from '../editor/monacoSetup.js'
import { docsPages } from '../docsContent.jsx'

const samples = {
  hello: `print("Hello, world!")

loc v = Vector2.new(3,4)
print(v.Magnitude) -- 5`,
  vectors: `loc a = Vector2.new(3,4)
loc b = Vector2.new(1,2)
print(a + b)
print(a:Dot(b))`,
  io: `loc f = io.open("hello.txt","w")
f:write("hi")
f:close()
print(io.readFile("hello.txt"))`,
  qualified: `func M.foo(a, b) { return a + b }
print(M.foo(2, 3))

try {
  error("something broke")
} catch (e) {
  print(e.message, e.line)
}`,
  demo: `func fizzbuzz(n) {
    loc i = 1
    while (i <= n) {
        if (i % 15 == 0) { print("FizzBuzz") }
        else if (i % 3 == 0) { print("Fizz") }
        else if (i % 5 == 0) { print("Buzz") }
        else { print(i) }
        i = i + 1
    }
}

fizzbuzz(15)`
}

const safeName = (n) => {
  const base = String(n).replace(/[\/\\]/g, '').replace(/^\.+/, '') || 'untitled.aul'
  return base
}

const fmtTime = (ms) => {
  if (ms < 1) return `${Math.round(ms * 1000)}µs`
  if (ms < 1000) return `${ms.toFixed(ms < 10 ? 2 : 1)}ms`
  return `${(ms / 1000).toFixed(2)}s`
}

const decodeShare = () => {
  if (location.hash.length <= 1) return null
  try {
    const raw = decodeURIComponent(escape(atob(location.hash.slice(1))))
    try {
      const data = JSON.parse(raw)
      if (data && typeof data.f === 'object' && Object.keys(data.f).length) {
        return { files: data.f, active: data.a }
      }
    } catch (e) { /* legacy raw-code hash */ }
    return { files: { 'main.aul': raw }, active: 'main.aul' }
  } catch (e) { return null }
}

export default function App(){
  const [view, setView] = useState('intro')
  const [files, setFiles] = useState({ 'main.aul': samples.hello })
  const [active, setActive] = useState('main.aul')
  const [out, setOut] = useState('Press Run to execute.')
  const [runInfo, setRunInfo] = useState('')
  const [outOpen, setOutOpen] = useState(true)
  const [running, setRunning] = useState(false)
  const [untitledN, setUntitledN] = useState(1)
  const [docPage, setDocPage] = useState('Installation')
  const [demoCode, setDemoCode] = useState(samples.demo)
  const [demoOut, setDemoOut] = useState(null)
  const [demoRunning, setDemoRunning] = useState(false)
  const [demoInfo, setDemoInfo] = useState('')

  useEffect(()=>{
    const shared = decodeShare()
    if (shared) {
      const clean = {}
      for (const [n, c] of Object.entries(shared.files)) clean[safeName(n)] = String(c)
      if (Object.keys(clean).length) {
        setFiles(clean)
        if (clean[shared.active]) setActive(shared.active)
        else setActive(Object.keys(clean)[0])
      }
    }
  },[])

  useEffect(()=>{
    aulFiles.list = Object.keys(files)
  },[files])

  const addTab = ()=>{
    let n = untitledN
    while (files[`untitled${n}.aul`]) n++
    const name = `untitled${n}.aul`
    setUntitledN(n + 1)
    setFiles((f)=>({ ...f, [name]: '-- new file\n' }))
    setActive(name)
  }

  const closeTab = (name)=>{
    setFiles((f)=>{
      const next = { ...f }
      delete next[name]
      const rest = Object.keys(next)
      if (active === name) setActive(rest[0])
      return next
    })
  }

  const renameTab = (name)=>{
    const input = prompt('File name:', name)
    if (input == null) return
    const newName = safeName(input)
    if (!newName || newName === name || files[newName]) return
    setFiles((f)=>{
      const next = {}
      for (const [n, c] of Object.entries(f)) next[n === name ? newName : n] = c
      return next
    })
    if (active === name) setActive(newName)
  }

  const share = async ()=>{
    const data = btoa(unescape(encodeURIComponent(JSON.stringify({ f: files, a: active }))))
    history.replaceState(null, '', '#' + data)
    await navigator.clipboard.writeText(location.href)
  }

  const run = async ()=>{
    setRunning(true)
    setOut('')
    setRunInfo('')
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))
    try {
      const aul = await ensureAul(import.meta.env.BASE_URL)
      for (const [name, content] of Object.entries(files)) {
        aul.writeFile('/' + safeName(name), content)
      }
      const t0 = performance.now()
      const res = aul.run(files[active])
      setRunInfo(fmtTime(performance.now() - t0))
      const lines = [`Running ${active}...`]
      if (res.output) lines.push(res.output)
      else lines.push(`(exit ${res.status}, no output)`)
      setOut(lines.join('\n'))
    } catch (e) {
      setOut('WASM build missing. Run `make wasm` (needs emsdk on PATH), rebuild the site, and reload.\n' + e.message)
    }
    setRunning(false)
    share().catch(()=>{})
  }

  const showBytecode = async ()=>{
    setRunning(true)
    setOut('')
    setRunInfo('')
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))
    try {
      const aul = await ensureAul(import.meta.env.BASE_URL)
      const res = aul.disasm(files[active])
      const lines = [`Bytecode for ${active}:`]
      if (res.output) lines.push(res.output)
      else lines.push(`(exit ${res.status}, no output)`)
      setOut(lines.join('\n'))
    } catch (e) {
      setOut('WASM build missing. Run `make wasm` (needs emsdk on PATH), rebuild the site, and reload.\n' + e.message)
    }
    setRunning(false)
  }

  const runDemo = async ()=>{
    setDemoRunning(true)
    setDemoOut(null)
    setDemoInfo('')
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))
    try {
      const aul = await ensureAul(import.meta.env.BASE_URL)
      const t0 = performance.now()
      const res = aul.run(demoCode)
      setDemoInfo(fmtTime(performance.now() - t0))
      setDemoOut(res.output || `(exit ${res.status}, no output)`)
    } catch (e) {
      setDemoOut('WASM build missing. Run `make wasm` and reload.\n' + e.message)
    }
    setDemoRunning(false)
  }

  return <>
    <nav>
      <button className="logo" onClick={()=>setView('intro')}><img src={`${import.meta.env.BASE_URL}logo.png`} alt="aul" /></button>
      <button className={view==='intro'?'active':''} onClick={()=>setView('intro')}>Intro</button>
      <button className={view==='docs'?'active':''} onClick={()=>setView('docs')}>Docs</button>
      <button className={view==='playground'?'active':''} onClick={()=>setView('playground')}>Playground</button>
      <a href="https://github.com/onked/aul" target="_blank">GitHub</a>
    </nav>
    {view!=='playground' && <main>
      {view==='intro' && <>
        <section className="hero">
          <img className="hero-logo" src={`${import.meta.env.BASE_URL}logo.png`} alt="Aul logo" />
          <h1>Simple, embeddable C script engine</h1>
          <p className="hero-sub">
            <code>Aul</code> is a fast, embeddable scripting language with a register VM,
            meant for modding, game development (soon), and embedded systems.
          </p>
          <div className="hero-cta">
            <button className="btn" onClick={()=>document.getElementById('start')?.scrollIntoView({behavior:'smooth'})}>Get Started</button>
            <a className="btn secondary" href="https://github.com/onked/aul" target="_blank">View on GitHub</a>
          </div>
        </section>

        <section id="start" className="sec">
          <div className="sec-label">// getting started</div>
          <div className="steps">
            <div className="step">
              <span className="step-num">1</span>
              <div>
                <b>Clone and build</b>: <code>git clone https://github.com/onked/aul</code>, then <code>make</code>. Uses mingw64.
              </div>
            </div>
            <div className="step">
              <span className="step-num">2</span>
              <div><b>Run a script</b>: <code>aul.exe hello.aul</code>. The same source compiles into your engine or the web playground.</div>
            </div>
            <div className="step">
              <span className="step-num">3</span>
              <div><b>Embed it</b>: copy the <code>source/</code> into your project, call <code>initVM()</code>, register your own C functions with <code>newNative</code>.</div>
            </div>
          </div>
        </section>

        <section className="sec">
          <div className="sec-label">// syntax, quickly</div>
          <div className="syn-grid">
            <div className="syn"><span className="syn-term"><i>loc</i> x = 10</span><span className="syn-desc">locals and <i>glo</i> globals</span></div>
            <div className="syn"><span className="syn-term"><i>func</i> f(a, ...) {"{"} ... {"}"}</span><span className="syn-desc">functions with varargs</span></div>
            <div className="syn"><span className="syn-term"><i>func</i> M.name()</span><span className="syn-desc">methods declared on tables</span></div>
            <div className="syn"><span className="syn-term"><i>while</i> / <i>for</i> / <i>for</i> (k, v in t)</span><span className="syn-desc">C-style loops and table iteration</span></div>
            <div className="syn"><span className="syn-term"><i>try</i> {"{"} ... {"}"} <i>catch</i> (e)</span><span className="syn-desc">errors with .message, .line, .traceback</span></div>
            <div className="syn"><span className="syn-term">`hi {"{name}"}`</span><span className="syn-desc">backtick string interpolation</span></div>
            <div className="syn"><span className="syn-term"><i>require</i>("mod")</span><span className="syn-desc">module loader with cache</span></div>
            <div className="syn"><span className="syn-term">io / os / math / string / table</span><span className="syn-desc">standard libraries, C</span></div>
          </div>
        </section>

        <section className="sec">
          <div className="sec-label">// benchmarks</div>
          <table className="bench">
            <thead>
              <tr><th>workload</th><th>time</th><th></th></tr>
            </thead>
            <tbody>
              <tr><td>100M integer additions</td><td className="num">~0.7s</td><td className="note">fast interpreted loop</td></tr>
              <tr><td>100k Vector2 additions</td><td className="num">~4ms</td><td className="note">native vectors, pooled</td></tr>
              <tr><td>1M function calls</td><td className="num">~45ms</td><td className="note">register based frames</td></tr>
              <tr><td>fib(26)</td><td className="num">~17ms</td><td className="note">recursion & closures</td></tr>
            </tbody>
          </table>
          <p className="bench-note">Measured natively at -O2 on Windows with a Ryzen 5 7500f.</p>
        </section>

        <section className="feat-row">
          <div className="feat">
            <h3>Fast</h3>
            <p>Incremental tri-color mark & sweep GC, meant for games, etc</p>
          </div>
          <div className="feat mid">
            <h3>Single binary</h3>
            <p>Compile, script and done. Very small file size meaning very portable</p>
          </div>
          <div className="feat mid">
            <h3>Embeddable</h3>
            <p>Drop the source into your engine, register C functions</p>
          </div>
        </section>

        <section className="demo">
          <div className="demo-head">
            <span className="demo-name">demo.aul</span>
            <span className="demo-fill" />
            {demoInfo && <span className="demo-info">{demoInfo}</span>}
            <button className="pg-run" onClick={runDemo} disabled={demoRunning}>{demoRunning ? 'Running…' : '▶ Run'}</button>
          </div>
          <AulEditor value={demoCode} onChange={setDemoCode} height="320px" />
          {demoOut !== null && <pre className="demo-out">{demoOut}</pre>}
        </section>
      </>}
      {view==='docs' && <div className="docs">
        <aside className="docs-side">
          {[
            ['getting started', ['Installation', 'Running scripts', 'Embedding']],
            ['language', ['Variables', 'Functions', 'Control flow', 'Errors', 'Strings', 'Tables', 'Closures', 'Modules']],
            ['libraries', ['Standard library', 'Vector2 / Vector3', 'File IO', 'OS']],
            ['internals', ['The VM', 'Garbage collector', 'Build flags']],
          ].map(([head, links]) => (
            <div key={head} className="docs-group">
              <div className="docs-head">{head}</div>
              {links.map((n) => (
                <a key={n} className={`docs-link ${n===docPage?'active':''}`} onClick={()=>setDocPage(n)}>{n}</a>
              ))}
            </div>
          ))}
        </aside>
        <div className="docs-body">
          {(() => {
            const page = docsPages[docPage]
            if (!page) return <p>Pick something from the sidebar.</p>
            return <>
              <h1 className="doc-title">{page.title}</h1>
              {page.blocks.map((b, i) => {
                if (b.type === 'code') return <pre key={i}><code>{b.text}</code></pre>
                if (b.type === 'ul') return <ul key={i}>{b.items.map((it, j) => <li key={j}>{it}</li>)}</ul>
                return <p key={i}>{b.text}</p>
              })}
            </>
          })()}
        </div>
      </div>}
    </main>}
    {view==='playground' && <>
      <div className="pg">
        <div className="pg-tabs">
          {Object.keys(files).map((name) => (
            <div key={name} className={`pg-tab ${name===active?'active':''}`} onClick={()=>setActive(name)} title="Double-click to rename">
              <span className="pg-tab-name" onDoubleClick={(e)=>{ e.stopPropagation(); renameTab(name) }}>{name}</span>
              <span className="pg-tab-close" onClick={(e)=>{ e.stopPropagation(); if (Object.keys(files).length > 1) closeTab(name) }}>×</span>
            </div>
          ))}
          <div className="pg-tab pg-tab-add" onClick={addTab}>+</div>
          <div className="pg-tabs-fill" />
          <div className="pg-actions">
            <button className="pg-bytecode" onClick={showBytecode} disabled={running}>Bytecode</button>
            <button className="pg-run" onClick={run} disabled={running}>{running ? 'Running…' : '▶ Run'}</button>
          </div>
        </div>
        <div className="pg-editor">
          <AulEditor value={files[active]} onChange={(v)=>setFiles((f)=>({ ...f, [active]: v }))} height="100%" />
        </div>
        <div className={`pg-out ${outOpen?'open':''}`}>
          <div className="pg-out-head" onClick={()=>setOutOpen((o)=>!o)}>
            <span className={`pg-caret ${outOpen?'open':''}`}>▾</span>
            <span>Output</span>
            {runInfo && <span className="pg-out-time">{runInfo}</span>}
            <span className="pg-out-fill" />
            <span className="pg-out-clear" onClick={(e)=>{ e.stopPropagation(); setOut(''); setRunInfo('') }}>Clear</span>
          </div>
          {outOpen && <pre className="pg-out-body">{out}</pre>}
        </div>
      </div>
    </>}
  </>
}
