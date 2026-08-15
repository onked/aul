#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/system.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"
#include "compiler.h"

static Table requireCache;

static char* readSourceFile(const char* chars, int length) {
    char* path = (char*)malloc((size_t)length + 5);
    memcpy(path, chars, (size_t)length);
    path[length] = '\0';

    FILE* file = fopen(path, "rb");
    if (file == NULL && (length < 4 || strcmp(path + length - 4, ".aul") != 0)) {
        strcat(path, ".aul");
        file = fopen(path, "rb");
    }
    if (file == NULL) {
        free(path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer != NULL) {
        size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
        if (bytesRead == fileSize) {
            buffer[bytesRead] = '\0';
        } else {
            free(buffer);
            buffer = NULL;
        }
    }
    fclose(file);
    free(path);
    return buffer;
}

static void sysError(int argCount, Value* args, Value* result) {
    *result = NIL_VAL;
    Value msg = (argCount >= 1) ? args[0] : NIL_VAL;
    vmRaiseError(msg);
}

static void sysAssert(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    Value v = args[0];
    if (IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v))) {
        Value msg = (argCount >= 2) ? args[1] : NIL_VAL;
        *result = NIL_VAL;
        vmRaiseError(msg);
        return;
    }
    *result = v;
}

static void sysRequire(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    if (!IS_STRING(args[0])) { *result = NIL_VAL; return; }
    ObjString* path = AS_STRING(args[0]);

    if (vm.frameCount >= FRAMES_MAX - 2) { *result = NIL_VAL; return; }

    Value cached;
    if (tableGet(&requireCache, OBJ_VAL((Obj*)path), &cached)) {
        *result = cached;
        return;
    }

    char* source = readSourceFile(path->chars, path->length);
    if (source == NULL) { *result = NIL_VAL; return; }

    ObjFunction* function = compile(source);
    if (function == NULL) {
        free(source);
        *result = NIL_VAL;
        return;
    }

    setCompiling(true);
    ObjClosure* closure = newClosure(function);
    setCompiling(false);

    int outerCount = vm.frameCount;
    CallFrame* nextFrame = &vm.frames[outerCount];
    CallFrame* caller = &vm.frames[outerCount - 1];
    nextFrame->closure = closure;
    nextFrame->slots = caller->slots + caller->closure->function->maxRegs;
    nextFrame->ip = closure->function->chunk.code;
    nextFrame->argCount = 0;
    nextFrame->varargCount = 0;
    vm.frameCount = outerCount + 1;

    int savedRunBase = vm.runBase;
    vm.runBase = outerCount;
    InterpretResult r = run(outerCount);
    vm.runBase = savedRunBase;
    vm.frameCount = outerCount;

    free(source);

    if (r == INTERPRET_RUNTIME_ERROR) {
        if (vm.pendingError != NIL_VAL) {
            vmReRaiseError();
            if (vm.pendingError == NIL_VAL) {
                *result = NIL_VAL;
                return;
            }
        }
        exit(70);
    }

    Value moduleResult = nextFrame->slots[0];

    setCompiling(true);
    ObjString* cacheKey = copyString(path->chars, path->length);
    tableSet(&requireCache, OBJ_VAL((Obj*)cacheKey), moduleResult);
    setCompiling(false);

    *result = moduleResult;
}

void initSystemLibrary(void) {
    initTable(&requireCache);

    ObjNative* requireNative = newNative("require", sysRequire, 1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("require", 7)), OBJ_VAL((Obj*)requireNative));
    ObjNative* errorNative = newNative("error", sysError, -1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("error", 5)), OBJ_VAL((Obj*)errorNative));
    ObjNative* assertNative = newNative("assert", sysAssert, -1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("assert", 6)), OBJ_VAL((Obj*)assertNative));
}