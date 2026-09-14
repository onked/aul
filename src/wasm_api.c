#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "vm.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "value.h"

static void dumpFunc(ObjFunction* fn, const char* name) {
    disassembleChunk(&fn->chunk, name);
    for (int i = 0; i < fn->chunk.constants.count; i++) {
        Value v = fn->chunk.constants.values[i];
        if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_FUNCTION) {
            ObjFunction* inner = AS_FUNCTION(v);
            dumpFunc(inner, inner->name ? inner->name->chars : "<anon>");
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void aul_init(void) {
    initVM();
}

EMSCRIPTEN_KEEPALIVE
int aul_run(const char* source) {
    return (int)interpret(source);
}

EMSCRIPTEN_KEEPALIVE
int aul_disasm(const char* source) {
    ObjFunction* fn = compile(source);
    if (fn == NULL) return 1;
    dumpFunc(fn, "<script>");
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void aul_free(void) {
    freeVM();
}
