<p align="center">
  <img width="180" alt="aul logo" src="docs/public/logo.png" />
</p>

<h1 align="center">Aul</h1>

<p align="center">A fast, small, embeddable scripting language with a register VM. Written in C, inspired by <a href="https://www.lua.org/">Lua.</a></p>

<p align="center">
  <a href="https://onked.github.io/aul">Docs & playground</a>
  ·
  <a href="LICENSE">MIT</a>
</p>

```aul
loc v = Vector2.new(3, 4)
print(v.Magnitude) -- 5

func M.greet(name) { return "hi " + name }

try {
    risky()
} catch (e) {
    print(e.message, e.line)
}
```

## Build

```sh
git clone https://github.com/onked/aul
cd aul
make # aul.exe: debug build (ASAN, -g)
make aulfast # aulfast.exe: optimized build (-O2)
make wasm # docs/public/aul.js + aul.wasm (needs emsdk on PATH)
```

```sh
aulfast.exe tests/hello.aul # run a script
aulfast.exe -d tests/hello.aul # dump bytecode
```

Requires mingw64 (GCC) on Windows. No other dependencies.

## Features

- **Register VM**: 250 registers per frame, computed-goto dispatch, inline caches
- **Integers and vectors, specialized**: the compiler proves types up front and emits `OP_INT_*` / `OP_VEC_*` opcodes, no runtime checks on hot paths
- **NaN-boxed values**: every value fits in one 64-bit word
- **Incremental GC**: tri-color mark & sweep, interleaved with execution, pooled vectors
- **Full stdlib**: `math`, `string`, `table`, `io`, `os`, plus `require` with a module cache
- **Errors as values**:`try/catch`, `error()`, `assert()`, with `.message` / `.line` / `.traceback`
- **Playground**: the same VM compiled to WebAssembly, running in the browser

## Benchmarks

Measured natively at -O2 on Windows (Ryzen 5 7500f):

| workload | time |
|---|---|
| 100M integer additions | ~0.7s |
| 100k Vector2 additions | ~4ms |
| 1M function calls | ~45ms |
| fib(26) | ~17ms |

## Embed

```c
#include "vm.h"

initVM();
initNativeLibraries();

InterpretResult r = interpret("print(\"hello\")");

freeVM();
```

## Layout

```
src/            VM, compiler, GC
include/        public headers
src/libs/       math, string, table, vector, io, os, system
src/compiler/   scanner, parser, optimizer, type inference
tests/          .aul test scripts (run with aulfast.exe)
docs/           Documentation site
```

## License

MIT - see [LICENSE](LICENSE).