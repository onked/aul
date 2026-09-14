const P = (title, ...blocks) => ({ title, blocks })

const code = (t) => ({ type: 'code', text: t })
const p = (t) => ({ type: 'p', text: t })
const ul = (items) => ({ type: 'ul', items })

export const docsPages = {
  Installation: P('Installation',
    p('aul builds from source with a single C compiler. Clone the repo and run make:'),
    code(`git clone https://github.com/onked/aul
cd aul
make`),
    p('This produces two binaries:'),
    ul([
      'aul.exe: debug build (ASAN, -g). Use this when something crashes and you want a stack trace.',
      'aulfast.exe: optimized build (-O2). Use this for everything else.',
    ]),
    p('You need mingw64 (GCC) on Windows.'),
    p('For the web playground there is a third target, which needs the Emscripten SDK on PATH:'),
    code('make wasm'),
    p('That compiles the same VM to WebAssembly.')),

  'Running scripts': P('Running scripts',
    p('Run a script by passing a path:'),
    code('aulfast.exe tests/hello.aul'),
    p('The compiler runs four optimization passes before execution (constant folding, integer specialization, compare jump folding, NOP removal), so what executes is already optimised bytecode.'),
    p('You can also dump the disassembly of any script to see exactly what the VM runs:'),
    code('aulfast.exe -d tests/hello.aul'),
    p('Exit codes: 65 compile error, 70 runtime error, 74 file I/O error, 0 success. Errors print a message plus a stack traceback with line numbers.')),

  Embedding: P('Embedding',
    p('The whole language is a few C files. Drop src/ and include/ into your project, build them.'),
    code(`#include "vm.h"

initVM(); // sets up globals, GC, stdlib
initNativeLibraries(); // math/string/table/vector/io/os

InterpretResult r = interpret("print(\\"hello\\")");

freeVM();`),
    p('Register your own C functions so scripts call them at C speeds:'),
    code(`void eng_draw(int argc, Value* args, Value* result) {
    ObjVector2* pos = AS_VECTOR2(args[0]);
    Renderer::Draw(pos->x, pos->y);
    *result = NIL_VAL;
}

ObjNative* n = newNative("draw", eng_draw, 2);
tableSet(&vm.globals, OBJ_VAL(copyString("draw", 4)), OBJ_VAL(n));`),
    p('From the script side engine.draw(pos) is one table lookup plus one call. Values are represented as NaN-boxed uint64_t; use the IS_*/AS_* macros in object.h to unpack.'),
    p('For a sandboxed engine build, skip initIOLibrary/initOSLibrary (or delete the libs) so scripts get no filesystem or shell access.')),

  Variables: P('Variables',
    p('Two kinds of declaration:'),
    code(`loc x = 10 -- local
glo y = 20 -- global`),
    p('Declarations support multiple assignment. Only a bare call on the right side forwards all return values; anything else takes the first:'),
    code(`func pair() { return 10, 20 }

loc a, b = pair() -- a=10, b=20
loc c, d = pair(), 99 -- c=10, d=99
loc e, f = 1 -- e=1, f=nil
loc g, h -- g=nil, h=nil`),
    p('Missing targets become nil. Extra return values are discarded. Locals are block-scoped to their function; globals live in the VM global table and are shared across require()d modules.')),

  Functions: P('Functions',
    p('Functions are declared with func. They are first-class values:'),
    code(`func add(a, b) { return a + b }
loc double = func(x) { return x * 2 }`),
    p('Qualified declarations attach functions to tables. This is how you build modules and methods:'),
    code(`loc M = {}
func M.greet(name) { return "hi " + name }
loc func M.secret() { return 42 } -- loc prefix: local binding`),
    p('Variadic functions take ... as the last parameter. Inside the function, ... is an expression holding the extra arguments:'),
    code(`func sum(a, ...) {
    loc b, c = ...
    return a + b + c
}
sum(1, 2, 3) -- 6`),
    p('return a, b, c yields multiple values. return f() forwards only the first value of f. Use loc x, y = f() to catch them all.')),

  'Control flow': P('Control flow',
    code(`if (x > 5) {
    print("big")
} else if (x > 0) {
    print("small")
} else {
    print("tiny")
}`),
    p('Loops: while, for, and for-in over tables.'),
    code(`loc i = 0
while (i < 5) {
    if (i == 3) { break }
    i = i + 1
}

for (loc k = 0; k < 3; k = k + 1) { print(k) }
for (loc i = 0; i < 10; i++) { print(i) }

for (k, v in t)  { print(k, v) } -- key, value
for (v in t)     { print(v) } -- value only`),
    p('break and continue work in all loops. Ternary:'),
    code(`loc msg = x > 5 ? "big" : "small"`)),

  Errors: P('Errors',
    p('error() raises a catchable error, assert() raises unless the condition is truthy:'),
    code(`try {
    assert(cond, "cond was false")
    error("something broke")
} catch (e) {
    print(e.message) -- the error text
    print(e.line) -- line it was raised on
    print(e.traceback) -- full stack traceback string
}`),
    p('The catch value is an error object. Index it with .message, .line and .traceback. Runtime errors (bad types, missing globals, division by zero) raise the same way.'),
    p('Raising inside a catch block rethrows to the next enclosing try. Uncaught errors print the message and traceback, then exit with code 70.')),

  Strings: P('Strings',
    p('Double-quoted literals with escape sequences:'),
    code('"line\\ntab\\tx41 = A"'),
    p('Backtick strings interpolate expressions:'),
    code('loc name = "world"\nprint(`hello {name}, {2 + 3}`) -- hello world, 5'),
    p('Interpolation nests ({"{`inner`}"}) and spans lines. \\{ \\} \\` escape to literals.'),
    p('The string library (also available as globals):'),
    ul([
      'string.len / sub / upper / lower / trim',
      'string.split / find / contains / startsWith / endsWith',
      'string.replace / repeat / reverse / format',
      'string.charCode / fromCharCode',
    ]),
    p('+ concatenates strings, and #s gives the length. tonumber("42") parses back.')),

  Tables: P('Tables',
    p('Tables are the one data structure or array and hash map in one:'),
    code(`loc t = {1, 2, 3} -- array part, 1-based
loc cfg = {name: "x", retries: 3} -- keyed part
t[1] = 99
cfg.name = "y"
print(#t) -- length of the array part`),
    p('Metatables hook into table behavior:'),
    code(`loc proto = {speak: func(self) { return "..." }}
loc obj = {}
setmetatable(obj, {__index: proto, __call: func(self, x) { return x * 2 }})

obj.speak() -- via __index
obj(21) -- via __call → 42`),
    p('Supported metamethods: __index, __newindex, __call, __len, __add, __sub, __mul, __div. obj:method(args) is the same as obj.method(obj, args).'),
    p('The table library: insert, remove, clear, find, maxn, getn, concat, sort, pack, create.')),

  Closures: P('Closures',
    p('Functions capture their enclosing locals by reference:'),
    code(`func makeCounter() {
    loc count = 0
    return func() {
        count = count + 1
        return count
    }
}

loc c = makeCounter()
c() -- 1
c() -- 2`),
    p('Each call to makeCounter gets a fresh count. If the compiler proves a captured local is never mutated, it captures by value instead making it cheaper and immune to the variable later changing.'),
    p('Closures are how the standard library does iteration and how module-private state stays private.')),

  Modules: P('Modules',
    p('require loads a script once, runs it, and caches its return value:'),
    code(`-- mymod.aul
loc M = {}
func M.greet() { return "hi" }
return M

-- main.aul
loc m = require("mymod")
m.greet()`),
    p('Paths are relative to the working directory; the .aul extension is optional. A second require of the same path returns the cached table. Module top-level code runs exactly once.'),
    p('Qualified func declarations build modules without the manual table:'),
    code(`loc M = {}
func M.greet() { return "hi" }
return M`),
    p('Or'),
    code(`loc M = {}
M.greet = func() { return "hi" }
return M
`)

),

  'Standard library': P('Standard library',
    p('All libraries are written in C and registered as globals plus tables. math.* and string.* are also callable bare (sqrt(4) and math.sqrt(4) are the same function).'),
    p('math:'),
    ul([
      'sqrt, sin, cos, tan, asin, acos, atan, atan2',
      'floor, ceil, abs, exp, log, log10, pow, fmod',
      'min, max, random, randomseed, deg, rad',
      'math.pi, math.huge',
    ]),
    p('string: len, sub, upper, lower, split, find, contains, replace, etc. (see Strings page)'),
    p('table: insert, remove, sort, concat, find, pack, create.'),
    p('Globals: tostring, tonumber, type, typeof, require, error, assert, clock().'),
    p('typeof distinguishes Vector2/Vector3; type calls them "vector".')),

  'Vector2 / Vector3': P('Vector2 / Vector3',
    p('Native vector types. Plain C structs, pooled allocation, operator overloading in the VM:'),
    code(`loc a = Vector2.new(3, 4)
loc b = Vector2.new(1, 2)

loc c = a + b -- Vector2(4, 6)
loc d = a * 2 -- scalar
loc e = 2 * a -- either side
print(a.Magnitude) -- 5
print(a.Unit) -- normalized copy, NaN components on zero
print(a.X, a.Y) -- 3, 4`),
    p('Methods:'),
    ul([
      'Dot(other), Cross(other): Cross returns a number for 2D, a vector for 3D',
      'Lerp(goal, alpha), Angle(other)',
      'Max, Min, Clamp, Floor, Ceil, Abs, Sign, FuzzyEq',
    ]),
    code(`loc v3 = Vector3.new(1, 2, 2)
print(v3.Magnitude) -- 3
print(v3:Cross(Vector3.new(2,0,0))) -- Vector3(0, 4, -4)`),
    p('type(v) is "vector"; typeof(v) is "Vector2" or "Vector3". The vector library (Luau-style) wraps the same code: vector.create, vector.magnitude, vector.normalize, vector.dot, vector.lerp, plus zero/one constants.')),

  'File IO': P('File IO',
    p('io.open returns a file handle with method sugar:'),
    code(`loc f = io.open("data.txt", "w")
f:write("hello\\n")
f:close()

loc r = io.open("data.txt", "r")
print(r:read()) -- one line
r:close()`),
    p('read modes: no arg or "*a" reads everything remaining, "*l" reads one line, a number reads that many bytes. Handles also support seek (with "set", "cur", "end") and flush.'),
    p('One-shot helpers:'),
    code(`io.readFile("data.txt") -- whole file as a string
io.writeFile("out.txt", s) --whole write
io.exists("data.txt")
io.list(".") -- directory listing as a table
io.remove / io.rename / io.mkdir`),
    p('Unclosed handles are closed by the garbage collector when swept. Handles print as <file path>.'),
    p('In an engine build this library can be dropped entirely — see Embedding.')),

  OS: P('OS',
    code(`os.args -- table of CLI args, plus .n
os.clock() --high-resolution timer, seconds
os.time() -- unix timestamp
os.sleep(50) -- milliseconds
os.exit(1)`),
    p('Environment and shell:'),
    code(`os.getenv("PATH")
os.setenv("KEY", "value")
os.execute("echo hi") -- returns exit code
os.getCwd() 
os.setCwd("dir")`),
    p('os.exit stops the process immediately. In the web playground it does nothing and os.execute always returns nil — there is no shell in a browser.'),
    p('Like File IO, the whole library can be skipped in an engine build.')),

  'The VM': P('The VM',
    p('Aul runs a register-based bytecode VM. The compiler emits instructions like OP_ADD dst, a, b operating on a 250-slot register window per call frame.'),
    p('The dispatch loop uses computed goto (a jump table) instead of a switch, and every instruction table lookup has a per-instruction inline cache keyed by table identity and a write-generation counter.'),
    p('Values are NaN-boxed: every value is one 64-bit word. Numbers, 48-bit integers, booleans, nil and object pointers all fit, so type checks are one AND and the whole VM stack stays in CPU cache.'),
    p('A dataflow pass at compile time proves which registers hold integers and rewrites generic ops to integer-only opcodes. Vectors get the same treatment with dedicated opcodes.'),
    p('Call frames are contiguous slices of one big stack.')),

  'Garbage collector': P('Garbage collector',
    p('Incremental tri-color mark-and-sweep. Instead of stopping the world, small GC steps interleave with VM dispatch: mark roots (VM stack, call frames, globals, interned strings, open upvalues), blacken the gray set, then sweep a few objects per step.'),
    p('Write barriers keep incremental marking correct: storing an object into a black table re-grays it.'),
    p('New objects allocated during sweep go on a separate list and rejoin the main heap after the cycle ends, so nothing mid-cycle is freed or missed.'),
    p('The heap grows at 2x when full, so amortized allocation cost stays flat as your script runs.')),

  'Build flags': P('Build flags',
    p('Three make targets:'),
    ul([
      'make: aul.exe, GCC, -g + static ASAN. Debug/asan builds.',
      'make aulfast: aulfast.exe, GCC, -O2. The one you ship.',
      'make wasm: docs/public/aul.js + aul.wasm via emcc, needs emsdk on PATH.',
    ]),
    p('The wasm target compiles everything except main.c with:'),
    code('-O3 -flto -sWASM=1 -sENVIRONMENT=web,node -sNO_EXIT_RUNTIME=1 \\\n-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB \\\n-sEXPORTED_RUNTIME_METHODS=ccall,FS \\\n-sEXPORTED_FUNCTIONS=_aul_init,_aul_run,_aul_free'),
    p('Under __EMSCRIPTEN__, os.exit and os.execute become no-ops instead of killing the runtime, and require errors yield nil rather than exiting.'),
    p('For an embeded build: compile the same sources into your project, skip initIOLibrary/initOSLibrary if you want a sandbox, and link src/wasm_api.c only for web targets.'))
}

export default docsPages
