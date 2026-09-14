CC = gcc
CFLAGS = -Wall -Wextra -g -static-libasan -Iinclude -Isrc/compiler

SOURCES = $(wildcard src/*.c) $(wildcard src/**/*.c)
NATIVE_SOURCES = $(filter-out src/wasm_api.c,$(SOURCES))

aul: $(NATIVE_SOURCES)
	$(CC) $(CFLAGS) $(NATIVE_SOURCES) -o aul.exe

aulfast: CFLAGS = -Wall -Wextra -O2 -Iinclude -Isrc/compiler
aulfast: $(NATIVE_SOURCES)
	$(CC) $(CFLAGS) $(NATIVE_SOURCES) -o aulfast.exe

# Run a script: `make run FILE=tests/meta_arith.aul`
run: aulfast
	./aulfast.exe $(FILE)

# Run the same script under the ASAN debug build
runasan: aul
	./aul.exe $(FILE)

EMCC = emcc
EMCFLAGS = -O3 -flto -D_GNU_SOURCE -Iinclude -Isrc/compiler \
	-sWASM=1 -sENVIRONMENT=web,node -sNO_EXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 \
	-sSTACK_SIZE=1MB -sDISABLE_EXCEPTION_CATCHING=1 \
	-sEXPORTED_RUNTIME_METHODS=ccall,FS \
	-sEXPORTED_FUNCTIONS=_aul_init,_aul_run,_aul_disasm,_aul_free
WASM_SOURCES = $(filter-out src/main.c src/wasm_api.c,$(SOURCES)) src/wasm_api.c

wasm: $(WASM_SOURCES)
	-mkdir docs\public
	$(EMCC) $(EMCFLAGS) $(WASM_SOURCES) -o docs/public/aul.js