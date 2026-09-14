function tokenizer() {
  return {
    tokenizer: {
      root: [
        [/\b(loc|glo|func|if|else|while|for|in|return|and|or|not|break|continue|try|catch|print)\b/, 'keyword'],
        [/\b(true|false|nil)\b/, 'constant'],
        [/\b(Vector2|Vector3|vector|io|os|math|string|table)\b/, 'type'],
        [/\b(tostring|tonumber|type|typeof|require|error|assert|clock|setmetatable|getmetatable)\b/, 'function'],
        [/\d+\.\d+/, 'number'],
        [/\d+/, 'number'],
        [/\.\.\./, 'operator'],
        [/\.(?=[a-zA-Z_])/, 'delimiter', '@member'],
        [/:(?=[a-zA-Z_]\w*\s*\()/, 'delimiter', '@method'],
        [/"/, 'string', '@dblString'],
        [/`/, 'string', '@tplString'],
        [/--\[=+\[/, 'comment', '@blockCommentEq'],
        [/--\[\[/, 'comment', '@blockComment'],
        [/--.*$/, 'comment'],
        [/(\+=|-=|\*=|\/=|==|!=|<=|>=|\+\+)/, 'operator'],
        [/[=<>!+\-*\/%#?:]/, 'operator'],
        [/[{}()\[\]]/, '@brackets'],
        [/[;,.]/, 'delimiter'],
        [/:/, 'delimiter'],
        [/[a-zA-Z_]\w*/, 'identifier'],
      ],
      member: [
        [/[a-zA-Z_]\w*(?=\s*\()/, 'function', '@pop'],
        [/[a-zA-Z_]\w*/, 'type', '@pop'],
      ],
      method: [[/[a-zA-Z_]\w*/, 'function', '@pop']],
      dblString: [
        [/[^\\"]+/, 'string'],
        [/\\x[0-9a-fA-F]{2}/, 'string.escape'],
        [/\\./, 'string.escape'],
        [/"/, 'string', '@pop'],
      ],
      tplString: [
        [/[^\\`{]+/, 'string'],
        [/\\./, 'string.escape'],
        [/\{/, 'delimiter.bracket', '@tplInterp'],
        [/`/, 'string', '@pop'],
      ],
      tplInterp: [
        [/\{/, 'delimiter.bracket', '@push'],
        [/\}/, 'delimiter.bracket', '@pop'],
        { include: 'root' },
      ],
      blockComment: [
        [/[^\]]+/, 'comment'],
        [/\]\]/, 'comment', '@pop'],
        [/\]/, 'comment'],
      ],
      blockCommentEq: [
        [/[^\]]+/, 'comment'],
        [/\]=+\]/, 'comment', '@pop'],
        [/\]/, 'comment'],
      ],
    },
  }
}

function langConfig(mon) {
  return {
    comments: { lineComment: '--', blockComment: ['--[[', ']]'] },
    brackets: [
      ['{', '}'],
      ['[', ']'],
      ['(', ')'],
    ],
    autoClosingPairs: [
      { open: '{', close: '}' },
      { open: '[', close: ']' },
      { open: '(', close: ')' },
      { open: '"', close: '"', notIn: ['string', 'comment'] },
      { open: '`', close: '`', notIn: ['string', 'comment'] },
    ],
    surroundingPairs: [
      { open: '{', close: '}' },
      { open: '[', close: ']' },
      { open: '(', close: ')' },
      { open: '"', close: '"' },
      { open: '`', close: '`' },
    ],
    autoCloseBefore: ';:.,=}])>` \n\t',
    wordPattern: /(-?\d*\.\d\w*)|([^\`\~\!\@\#\%\^\&\*\(\)\-\=\+\[\{\]\}\\\|\;\:\'\"\,\.\<\>\/\?\s]+)/,
    indentationRules: {
      increaseIndentPattern: /^.*(\{[^}"'`]*|\([^)"'`]*|\[[^\]"'`]*)$/,
      decreaseIndentPattern: /^\s*[}\]]/,
    },
    folding: {
      markers: { start: /^\s*--\s*#?region\b/, end: /^\s*--\s*#?endregion\b/ },
    },
    onEnterRules: [
      {
        beforeText: /^\s*func\s+[\w.]+\(.*\)\s*$/,
        afterText: /^\s*$/,
        action: {
          indentAction: mon.languages.IndentAction.Indent,
          appendText: '{}',
        },
      },
    ],
  }
}

const KEYWORDS = [
  'loc', 'glo', 'func', 'if', 'else', 'while', 'for', 'in', 'return',
  'and', 'or', 'not', 'break', 'continue', 'try', 'catch', 'print',
]
const LITERALS = ['true', 'false', 'nil']

const SIG = {}
function def(owner, name, sig, params, doc) {
  const key = owner ? owner + '.' + name : name
  ;(SIG[key] || (SIG[key] = [])).push({ sig, params, doc })
}
function sigs() {
  if (sigs.done) return
  sigs.done = true
  const M = (name, sig, params, doc) => {
    def('math', name, sig, params, doc)
    if (name !== 'type') def(null, name, sig, params, doc)
  }
  M('sqrt', 'sqrt(x: number) -> number', ['x: number'], 'Square root of x.')
  M('sin', 'sin(x: number) -> number', ['x: number'], 'Sine (radians).')
  M('cos', 'cos(x: number) -> number', ['x: number'], 'Cosine (radians).')
  M('tan', 'tan(x: number) -> number', ['x: number'], 'Tangent (radians).')
  M('asin', 'asin(x: number) -> number', ['x: number'], 'Arc sine.')
  M('acos', 'acos(x: number) -> number', ['x: number'], 'Arc cosine.')
  M('atan', 'atan(x: number) -> number', ['x: number'], 'Arc tangent.')
  M('atan2', 'atan2(y: number, x: number) -> number', ['y: number', 'x: number'], 'Angle whose tangent is y/x.')
  M('sinh', 'sinh(x: number) -> number', ['x: number'], 'Hyperbolic sine.')
  M('cosh', 'cosh(x: number) -> number', ['x: number'], 'Hyperbolic cosine.')
  M('tanh', 'tanh(x: number) -> number', ['x: number'], 'Hyperbolic tangent.')
  M('abs', 'abs(x: number) -> number', ['x: number'], 'Absolute value.')
  M('floor', 'floor(x: number) -> number', ['x: number'], 'Rounds down.')
  M('ceil', 'ceil(x: number) -> number', ['x: number'], 'Rounds up.')
  M('exp', 'exp(x: number) -> number', ['x: number'], 'e raised to x.')
  M('log', 'log(x: number) -> number', ['x: number'], 'Natural logarithm. Pass a base as 2nd arg for other bases.')
  M('log10', 'log10(x: number) -> number', ['x: number'], 'Base-10 logarithm.')
  M('pow', 'pow(x: number, y: number) -> number', ['x: number', 'y: number'], 'x raised to y.')
  M('min', 'min(...) -> number', ['...: number'], 'Smallest of the arguments.')
  M('max', 'max(...) -> number', ['...: number'], 'Largest of the arguments.')
  M('deg', 'deg(x: number) -> number', ['x: number'], 'Radians to degrees.')
  M('rad', 'rad(x: number) -> number', ['x: number'], 'Degrees to radians.')
  M('fmod', 'fmod(x: number, y: number) -> number', ['x: number', 'y: number'], 'Float remainder of x/y.')
  M('randomseed', 'randomseed(seed: number)', ['seed: number'], 'Seeds the RNG.')
  M('type', 'math.type(x) -> string?', ['x: any'], "'integer' or 'float' for numbers.")
  def(null, 'random', 'random() -> number', [], 'Random float in [0, 1).')
  def(null, 'random', 'random(n: number) -> number', ['n: number'], 'Random integer in 1..n.')
  def(null, 'random', 'random(lo: number, hi: number) -> number', ['lo: number', 'hi: number'], 'Random integer in lo..hi.')
  def('math', 'pi', 'pi: number', [], '3.14159...')
  def('math', 'huge', 'huge: number', [], 'Infinity.')
  const S = (name, sig, params, doc) => { def('string', name, sig, params, doc); def(null, name, sig, params, doc) }
  S('len', 'len(s: string) -> number', ['s: string'], 'Length in bytes.')
  S('sub', 'sub(s: string, i: number [, j: number]) -> string', ['s: string', 'i: number', 'j: number?'], 'Substring (1-based, negative counts from end).')
  S('upper', 'upper(s: string) -> string', ['s: string'], 'Uppercase copy.')
  S('lower', 'lower(s: string) -> string', ['s: string'], 'Lowercase copy.')
  S('trim', 'trim(s: string) -> string', ['s: string'], 'Strips surrounding whitespace.')
  S('split', 'split(s: string [, sep: string]) -> table', ['s: string', 'sep: string?'], 'Splits into an array table.')
  S('find', 'find(s: string, needle: string [, start: number]) -> number?', ['s: string', 'needle: string', 'start: number?'], '1-based position or nil.')
  S('contains', 'contains(s: string, needle: string) -> boolean', ['s: string', 'needle: string'], 'Substring check.')
  S('startsWith', 'startsWith(s: string, prefix: string) -> boolean', ['s: string', 'prefix: string'], 'Prefix check.')
  S('endsWith', 'endsWith(s: string, suffix: string) -> boolean', ['s: string', 'suffix: string'], 'Suffix check.')
  S('replace', 'replace(s: string, from: string, to: string) -> string', ['s: string', 'from: string', 'to: string'], 'Replaces all occurrences.')
  S('repeat', 'repeat(s: string, n: number) -> string', ['s: string', 'n: number'], 'Repeats s n times (alias: rep).')
  S('rep', 'rep(s: string, n: number) -> string', ['s: string', 'n: number'], 'Alias of repeat.')
  S('reverse', 'reverse(s: string) -> string', ['s: string'], 'Reversed copy.')
  S('format', 'format(fmt: string, ...) -> string', ['fmt: string', '...: any'], '%s %d %f %g %e and %% escapes.')
  S('charCode', 'charCode(s: string, i: number) -> number', ['s: string', 'i: number'], 'Byte value at position i (alias: byte).')
  S('byte', 'byte(s: string, i: number) -> number', ['s: string', 'i: number'], 'Alias of charCode.')
  S('fromCharCode', 'fromCharCode(...) -> string', ['...: number'], 'Builds a string from byte values (alias: char).')
  S('char', 'char(...) -> string', ['...: number'], 'Alias of fromCharCode.')
  const T = (name, sig, params, doc) => def('table', name, sig, params, doc)
  T('insert', 'insert(t: table, v: any)', ['t: table', 'v: any'], 'Appends v.')
  T('insert', 'insert(t: table, pos: number, v: any)', ['t: table', 'pos: number', 'v: any'], 'Inserts v at pos.')
  T('remove', 'remove(t: table [, pos: number])', ['t: table', 'pos: number?'], 'Removes and returns an element.')
  T('clear', 'clear(t: table)', ['t: table'], 'Removes all elements.')
  T('find', 'find(t: table, v: any) -> number?', ['t: table', 'v: any'], 'Array position of v or nil.')
  T('maxn', 'maxn(t: table) -> number', ['t: table'], 'Highest set array index.')
  T('getn', 'getn(t: table) -> number', ['t: table'], 'Array length.')
  T('concat', 'concat(t: table [, sep: string]) -> string', ['t: table', 'sep: string?'], 'Joins array elements.')
  T('sort', 'sort(t: table)', ['t: table'], 'Sorts the array part in place.')
  T('pack', 'pack(...) -> table', ['...: any'], 'Packs args into an array table (+n field).')
  T('create', 'create(n: number [, fill: any]) -> table', ['n: number', 'fill: any?'], 'Pre-sized array table.')
  const I = (name, sig, params, doc) => def('io', name, sig, params, doc)
  I('open', 'open(path: string [, mode: string]) -> file?', ['path: string', 'mode: string?'], 'Opens a file ("r", "w", "a", "r+", ...). Nil on failure.')
  I('close', 'close(f: file)', ['f: file'], 'Closes the handle.')
  I('read', 'read(f: file [, mode]) -> string?', ['f: file', 'mode: any?'], 'Reads all ("*a"), a line ("*l"), or n bytes.')
  I('write', 'write(f: file, ...)', ['f: file', '...: string'], 'Writes strings. Returns the handle.')
  I('flush', 'flush(f: file)', ['f: file'], 'Flushes buffered output.')
  I('seek', 'seek(f: file [, whence [, offset]]) -> number', ['f: file', 'whence: string?', 'offset: number?'], 'whence is "set", "cur" or "end". Returns position.')
  I('exists', 'exists(path: string) -> boolean', ['path: string'], 'Path existence check.')
  I('remove', 'remove(path: string) -> boolean', ['path: string'], 'Deletes a file.')
  I('rename', 'rename(old: string, new: string) -> boolean', ['old: string', 'new: string'], 'Renames a file.')
  I('mkdir', 'mkdir(path: string) -> boolean', ['path: string'], 'Creates a directory.')
  I('list', 'list(path: string) -> table', ['path: string'], 'Directory listing as an array.')
  I('readFile', 'readFile(path: string) -> string?', ['path: string'], 'Reads a whole file. Nil on failure.')
  I('writeFile', 'writeFile(path: string, data: string) -> boolean', ['path: string', 'data: string'], 'Writes a whole file.')
  const O = (name, sig, params, doc) => def('os', name, sig, params, doc)
  O('exit', 'exit([code: number])', ['code: number?'], 'Exits the process.')
  O('sleep', 'sleep(ms: number)', ['ms: number'], 'Sleeps milliseconds.')
  O('clock', 'clock() -> number', [], 'High-resolution timer in seconds.')
  O('time', 'time() -> number', [], 'Unix timestamp.')
  O('getenv', 'getenv(name: string) -> string?', ['name: string'], 'Environment variable or nil.')
  O('setenv', 'setenv(name: string, value: string) -> boolean', ['name: string', 'value: string'], 'Sets an environment variable.')
  O('execute', 'execute(cmd: string) -> number', ['cmd: string'], 'Runs a shell command. Returns exit code.')
  O('getCwd', 'getCwd() -> string', [], 'Current working directory.')
  O('getcwd', 'getcwd() -> string', [], 'Alias of getCwd.')
  O('setCwd', 'setCwd(path: string) -> boolean', ['path: string'], 'Changes directory.')
  O('setcwd', 'setcwd(path: string) -> boolean', ['path: string'], 'Alias of setCwd.')
  O('args', 'args: table', [], 'CLI args (0-based) plus n field.')
  const V = (name, sig, params, doc) => def('vector', name, sig, params, doc)
  V('create', 'create(x: number, y: number, z: number) -> vector', ['x: number', 'y: number', 'z: number'], 'Makes a 3D vector.')
  V('magnitude', 'magnitude(v: vector) -> number', ['v: vector'], 'Length.')
  V('normalize', 'normalize(v: vector) -> vector', ['v: vector'], 'Unit vector.')
  V('cross', 'cross(a: vector, b: vector) -> vector', ['a: vector', 'b: vector'], 'Cross product.')
  V('dot', 'dot(a: vector, b: vector) -> number', ['a: vector', 'b: vector'], 'Dot product.')
  V('angle', 'angle(a: vector, b: vector [, axis: vector]) -> number', ['a: vector', 'b: vector', 'axis: vector?'], 'Angle in radians (signed around axis).')
  V('floor', 'floor(v: vector) -> vector', ['v: vector'], 'Per-component floor.')
  V('ceil', 'ceil(v: vector) -> vector', ['v: vector'], 'Per-component ceil.')
  V('abs', 'abs(v: vector) -> vector', ['v: vector'], 'Per-component abs.')
  V('sign', 'sign(v: vector) -> vector', ['v: vector'], 'Per-component sign.')
  V('clamp', 'clamp(v: vector, lo: vector, hi: vector) -> vector', ['v: vector', 'lo: vector', 'hi: vector'], 'Per-component clamp.')
  V('max', 'max(...) -> vector', ['...: vector'], 'Per-component max.')
  V('min', 'min(...) -> vector', ['...: vector'], 'Per-component min.')
  V('lerp', 'lerp(a: vector, b: vector, t: number) -> vector', ['a: vector', 'b: vector', 't: number'], 'Linear interpolation.')
  V('zero', 'zero: vector', [], 'Vector3(0, 0, 0).')
  V('one', 'one: vector', [], 'Vector3(1, 1, 1).')
  V('zero2', 'zero2: vector', [], 'Vector2(0, 0).')
  V('one2', 'one2: vector', [], 'Vector2(1, 1).')
  for (const owner of ['Vector2', 'Vector3']) {
    const T3 = owner === 'Vector3'
    const VT = T3 ? 'Vector3' : 'Vector2'
    const xyz = T3 ? ', z: number' : ''
    const W = (name, sig, params, doc) => def(owner, name, sig, params, doc)
    W('new', `new(x: number, y: number${xyz}) -> ${VT}`, ['x: number', 'y: number'].concat(T3 ? ['z: number'] : []), `Makes a ${T3 ? '3D' : '2D'} vector (missing args default to 0).`)
    const dot2 = (lname, uname, sig, params, doc) => { W(lname, sig, params, doc); W(uname, sig, params, doc) }
    dot2('dot', 'Dot', `Dot(other: ${VT}) -> number`, [`other: ${VT}`], 'Dot product.')
    if (T3) dot2('cross', 'Cross', `Cross(other: ${VT}) -> ${VT}`, [`other: ${VT}`], 'Cross product.')
    else dot2('cross', 'Cross', `Cross(other: ${VT}) -> number`, [`other: ${VT}`], '2D cross (scalar z-component).')
    dot2('lerp', 'Lerp', `Lerp(goal: ${VT}, alpha: number) -> ${VT}`, [`goal: ${VT}`, 'alpha: number'], 'Linear interpolation.')
    dot2('magnitude', 'Magnitude', `Magnitude() -> number`, [], 'Length. Also readable as .Magnitude.')
    dot2('unit', 'Unit', `Unit() -> ${VT}`, [], 'Normalized copy. NaN components when length is 0.')
    dot2('normalize', 'Normalize', `Normalize() -> ${VT}`, [], 'Alias of Unit.')
    dot2('angle', 'Angle', `Angle(other: ${VT}) -> number`, [`other: ${VT}`], 'Angle in radians.')
    dot2('max', 'Max', `Max(other: ${VT}) -> ${VT}`, [`other: ${VT}`], 'Per-component max.')
    dot2('min', 'Min', `Min(other: ${VT}) -> ${VT}`, [`other: ${VT}`], 'Per-component min.')
    dot2('clamp', 'Clamp', `Clamp(lo: ${VT}, hi: ${VT}) -> ${VT}`, [`lo: ${VT}`, `hi: ${VT}`], 'Per-component clamp.')
    dot2('floor', 'Floor', `Floor() -> ${VT}`, [], 'Per-component floor.')
    dot2('ceil', 'Ceil', `Ceil() -> ${VT}`, [], 'Per-component ceil.')
    dot2('abs', 'Abs', `Abs() -> ${VT}`, [], 'Per-component abs.')
    dot2('sign', 'Sign', `Sign() -> ${VT}`, [], 'Per-component sign.')
    dot2('fuzzyEq', 'FuzzyEq', `FuzzyEq(other: ${VT} [, eps: number]) -> boolean`, [`other: ${VT}`, 'eps: number?'], 'Approximate equality.')
    W('X', 'X: number', [], 'X component.')
    W('Y', 'Y: number', [], 'Y component.')
    if (T3) W('Z', 'Z: number', [], 'Z component.')
  }
  def(null, 'tostring', 'tostring(v: any) -> string', ['v: any'], 'String representation.')
  def(null, 'tonumber', 'tonumber(v: any) -> number?', ['v: any'], 'Parses a string. Numbers pass through.')
  def(null, 'type', 'type(v: any) -> string', ['v: any'], 'nil, boolean, number, string, table, function, vector, file.')
  def(null, 'typeof', 'typeof(v: any) -> string', ['v: any'], 'Like type, but Vector2/Vector3 stay distinct.')
  def(null, 'require', 'require(path: string) -> any', ['path: string'], 'Loads a module (cached).')
  def(null, 'error', 'error(msg: any)', ['msg: any'], 'Raises a catchable error.')
  def(null, 'assert', 'assert(cond: any [, msg: any])', ['cond: any', 'msg: any?'], 'Raises unless cond is truthy.')
  def(null, 'clock', 'clock() -> number', [], 'High-resolution timer in seconds.')
}
function members(owner) {
  const prefix = owner + '.'
  return Object.keys(SIG).filter((k) => k.startsWith(prefix)).map((k) => k.slice(prefix.length))
}
function memberKind(name) {
  if (['pi', 'huge', 'zero', 'one', 'zero2', 'one2'].includes(name)) return 'const'
  if (['X', 'Y', 'Z', 'Magnitude', 'magnitude', 'Unit', 'unit', 'args'].includes(name)) return 'prop'
  return 'method'
}

const SNIPPETS = [
  { n: 'func', body: 'func ${1:name}(${2:args}) {\n\t$0\n}', d: 'function declaration' },
  { n: 'if', body: 'if (${1:cond}) {\n\t$0\n}', d: 'if block' },
  { n: 'ifelse', body: 'if (${1:cond}) {\n\t$2\n} else {\n\t$0\n}', d: 'if/else block' },
  { n: 'while', body: 'while (${1:cond}) {\n\t$0\n}', d: 'while loop' },
  { n: 'for', body: 'for (loc ${1:i} = 0; ${1:i} < ${2:n}; ${1:i} = ${1:i} + 1) {\n\t$0\n}', d: 'C-style for loop' },
  { n: 'forin', body: 'for (${1:k}, ${2:v} in ${3:table}) {\n\t$0\n}', d: 'for-in loop' },
  { n: 'try', body: 'try {\n\t$1\n} catch (${2:e}) {\n\t$0\n}', d: 'try/catch' },
]

const keywordDocs = {
  loc: 'Declares local variables: `loc x = 1` or `loc a, b = f()`.',
  glo: 'Declares a global variable.',
  func: 'Declares a function. Qualified form defines a method: `func M.foo(a) { }`.',
  if: 'Conditional: `if (cond) { } else if { } else { }`.',
  else: 'Else branch of an if.',
  while: 'Loop while the condition holds. `break` / `continue` supported.',
  for: 'C-style `for (loc i = 0; i < n; i++)` or `for (k, v in t)`.',
  in: 'Iteration: `for (k, v in table)`.',
  return: 'Returns values. `return a, b` yields multiple values to `loc a, b = f()`.',
  and: 'Logical and (short-circuits).',
  or: 'Logical or (short-circuits).',
  not: 'Logical not.',
  break: 'Exits the enclosing loop.',
  continue: 'Skips to the next loop iteration.',
  try: 'Protected block: `try { } catch (e) { }`.',
  catch: 'Error handler. The bound value has `.message`, `.line`, `.traceback`.',
  print: 'Prints values separated by spaces, then a newline.',
  true: 'Boolean true.', false: 'Boolean false.', nil: 'Absence of value.',
}
const libDocs = {
  Vector2: 'Native 2D vector type. Construct with `Vector2.new(x, y)`.',
  Vector3: 'Native 3D vector type. Construct with `Vector3.new(x, y, z)`.',
  vector: 'Generic vector helpers (Luau-style). See `vector.create`.',
  io: 'File I/O. Handles support `f:read()`, `f:write()`, `f:close()`.',
  os: 'OS access: time, sleep, env, processes, working directory.',
  math: 'Math functions (also available as bare globals).',
  string: 'String functions (also available as bare globals).',
  table: 'Array/hash helpers.',
}

function sigOf(key) {
  const e = SIG[key]
  return e && e.length ? e[0].sig : key
}

let providersRegistered = false

export const aulFiles = { list: [] }

function registerProviders(mon) {
  if (providersRegistered) return
  providersRegistered = true
  sigs()
  const K = mon.languages.CompletionItemKind
  const SnippetRule = mon.languages.CompletionItemInsertTextRule.InsertAsSnippet
  const kindFor = (name) => {
    const k = memberKind(name)
    return k === 'const' ? K.Constant : k === 'prop' ? K.Property : K.Method
  }

  const globalNames = Object.keys(SIG).filter((k) => !k.includes('.'))

  mon.languages.registerCompletionItemProvider('aul', {
    triggerCharacters: ['.', ':'],
    provideCompletionItems(model, position) {
      const line = model.getLineContent(position.lineNumber)
      const before = line.slice(0, position.column - 1)
      const word = model.getWordUntilPosition(position)
      const range = {
        startLineNumber: position.lineNumber,
        endLineNumber: position.lineNumber,
        startColumn: word.startColumn,
        endColumn: word.endColumn,
      }

      const m = before.match(/([A-Za-z_]\w*)\s*([:.])\s*[A-Za-z_]*$/)
      if (m && members(m[1]).length) {
        const owner = m[1]
        return {
          suggestions: members(owner).map((n) => ({
            label: n,
            kind: kindFor(n),
            detail: `${owner}.${sigOf(owner + '.' + n)}`,
            insertText: n,
            range,
          })),
        }
      }
      if (m) return undefined

      // `require("` inside an open string → suggest the other tabs
      const req = before.match(/require\(\s*"([^"]*)$/)
      if (req) {
        const typed = req[1]
        const reqRange = {
          startLineNumber: position.lineNumber,
          endLineNumber: position.lineNumber,
          startColumn: position.column - typed.length,
          endColumn: position.column,
        }
        if (aulFiles.list.length) {
          return {
            suggestions: aulFiles.list.map((n) => ({
              label: n,
              kind: K.File,
              detail: 'module in this playground',
              insertText: n.replace(/\.aul$/, ''),
              range: reqRange,
            })),
          }
        }
      }

      const suggestions = [
        ...KEYWORDS.map((n) => ({
          label: n, kind: K.Keyword, detail: keywordDocs[n], insertText: n, range,
        })),
        ...LITERALS.map((n) => ({
          label: n, kind: K.Keyword, detail: keywordDocs[n], insertText: n, range,
        })),
        ...['Vector2', 'Vector3'].map((n) => ({
          label: n, kind: K.Class, detail: libDocs[n], insertText: n, range,
        })),
        ...['vector', 'io', 'os', 'math', 'string', 'table'].map((n) => ({
          label: n, kind: K.Module, detail: libDocs[n], insertText: n, range,
        })),
        ...globalNames.map((n) => ({
          label: n, kind: K.Function, detail: sigOf(n), insertText: n, range,
        })),
        ...SNIPPETS.map((s) => ({
          label: s.n,
          kind: K.Snippet,
          detail: s.d,
          insertText: s.body,
          insertTextRules: SnippetRule,
          range,
        })),
      ]
      return { suggestions }
    },
  })
  mon.languages.registerSignatureHelpProvider('aul', {
    signatureHelpTriggerCharacters: ['(', ','],
    signatureHelpRetriggerCharacters: [','],
    provideSignatureHelp(model, position) {
      const raw = model.getLineContent(position.lineNumber).slice(0, position.column - 1)
      const line = raw
        .replace(/"(?:[^"\\]|\\.)*"/g, '""')
        .replace(/`(?:[^`\\]|\\.)*`/g, '``')
        .replace(/--.*$/, '')
      let depth = 0
      let activeParam = 0
      let i = line.length - 1
      for (; i >= 0; i--) {
        const ch = line[i]
        if (ch === ')') depth++
        else if (ch === '(') {
          if (depth === 0) break
          depth--
        } else if (ch === ',' && depth === 0) activeParam++
        else if ((ch === ';' || ch === '{' || ch === '}') && depth === 0) return undefined
      }
      if (i < 0) return undefined
      const head = line.slice(0, i)
      const nm = head.match(/([A-Za-z_]\w*)\s*$/)
      if (!nm) return undefined
      const rest = head.slice(0, head.length - nm[1].length)
      const ow = rest.match(/([A-Za-z_]\w*)\s*[:.]\s*$/)
      const key = ow ? ow[1] + '.' + nm[1] : nm[1]
      const overloads = SIG[key]
      if (!overloads || !overloads.length) return undefined
      return {
        value: {
          signatures: overloads.map((o) => ({
            label: `ƒ ${o.sig}`,
            documentation: { value: o.doc },
            parameters: o.params.map((p) => ({ label: p })),
          })),
          activeSignature: 0,
          activeParameter: Math.min(activeParam, Math.max(0, overloads[0].params.length - 1)),
        },
        dispose() {},
      }
    },
  })

  mon.languages.registerHoverProvider('aul', {
    provideHover(model, position) {
      const word = model.getWordAtPosition(position)
      if (!word || !word.word) return undefined
      const name = word.word
      const line = model.getLineContent(position.lineNumber)
      const before = line.slice(0, word.startColumn - 1)
      const ow = before.match(/([A-Za-z_]\w*)\s*[:.]\s*$/)
      const range = {
        startLineNumber: position.lineNumber,
        endLineNumber: position.lineNumber,
        startColumn: word.startColumn,
        endColumn: word.endColumn,
      }
      const show = (title, doc) => ({
        range,
        contents: [{ value: '```aul\n' + title + '\n```\n\n' + doc }],
      })
      if (ow && SIG[ow[1] + '.' + name] && SIG[ow[1] + '.' + name].length) {
        const o = SIG[ow[1] + '.' + name][0]
        return show(`${ow[1]}.${o.sig}`, o.doc)
      }
      if (!ow && SIG[name] && SIG[name].length) {
        const o = SIG[name][0]
        return show(o.sig, o.doc)
      }
      if (!ow && keywordDocs[name]) return show(name, keywordDocs[name])
      if (!ow && libDocs[name]) return show(name, libDocs[name])
      return undefined
    },
  })
}

let langConfigured = false

export function setupAulLanguage(m) {
  const mon = m
  if (!mon.languages.getLanguages().some((l) => l.id === 'aul')) {
    mon.languages.register({ id: 'aul' })
    mon.languages.setMonarchTokensProvider('aul', tokenizer())
  }
  
  mon.editor.defineTheme('aul-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'keyword', foreground: 'FF7AB2' },
      { token: 'constant', foreground: '79C0FF' },
      { token: 'type', foreground: '7EE787' },
      { token: 'function', foreground: 'D2A8FF' },
      { token: 'comment', foreground: '8B949E', fontStyle: 'italic' },
      { token: 'string.escape', foreground: 'FFA657' },
    ],
    colors: {},
  })
  if (!langConfigured) {
    langConfigured = true
    mon.languages.setLanguageConfiguration('aul', langConfig(mon))
    sigs()
    registerProviders(mon)
  }
}
