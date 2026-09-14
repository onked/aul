import { Component } from 'react'
import Editor from '@monaco-editor/react'
import { setupAulLanguage } from '../editor/monacoSetup.js'

class EditorBoundary extends Component {
  constructor(props) {
    super(props)
    this.state = { crashed: null }
  }
  static getDerivedStateFromError(err) {
    return { crashed: err }
  }
  componentDidCatch(err, info) {
    console.error('Monaco crashed, using textarea fallback:', err, info)
  }
  render() {
    if (this.state.crashed) {
      return (
        <textarea
          value={this.props.value}
          onChange={(e) => this.props.onChange(e.target.value)}
          spellCheck={false}
          style={{
            width: '100%', height: 340, background: '#010409', color: '#c9d1d9',
            border: '1px solid #30363d', borderRadius: 8, padding: 12,
            font: '13px ui-monospace,Consolas,monospace', resize: 'vertical',
          }}
        />
      )
    }
    return this.props.children
  }
}

export default function AulEditor({ value, onChange, height }) {
  const h = height || '340px'
  return (
    <EditorBoundary value={value} onChange={onChange}>
      <Editor
        height={h}
        language="aul"
        value={value}
        onChange={(v) => onChange(v || '')}
        beforeMount={setupAulLanguage}
        onMount={(editor, monaco) => monaco.editor.setTheme('aul-dark')}
        loading={<div style={{ height: h, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#8b949e' }}>Loading editor…</div>}
            options={{
              minimap: { enabled: false },
              fontSize: 13,
              wordWrap: 'on',
              automaticLayout: true,
              autoClosingBrackets: 'languageDefined',
              autoClosingQuotes: 'languageDefined',
              autoClosingOvertype: 'always',
              autoSurround: 'languageDefined',
              autoIndent: 'full',
              matchBrackets: 'always',
              tabSize: 4,
              insertSpaces: true,
            }}
      />
    </EditorBoundary>
  )
}
