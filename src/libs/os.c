#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "libs/os.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"

static void osExit(int argCount, Value* args, Value* result) {
    int code = 0;
    if (argCount >= 1 && IS_INTEGER(args[0])) code = (int)AS_INTEGER(args[0]);
    else if (argCount >= 1 && IS_NUMBER(args[0])) code = (int)AS_NUMBER(args[0]);
#ifdef __EMSCRIPTEN__
    (void)code;
    *result = NIL_VAL;
    return;
#else
    exit(code);
    *result = NIL_VAL;
#endif
}

static void osSleep(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    double ms = 0;
    if (IS_INTEGER(args[0])) ms = (double)AS_INTEGER(args[0]);
    else if (IS_NUMBER(args[0])) ms = AS_NUMBER(args[0]);
    else { *result = NIL_VAL; return; }
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
    *result = NIL_VAL;
}

static void osClock(int argCount, Value* args, Value* result) {
    (void)argCount; (void)args;
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    *result = NUMBER_VAL((double)counter.QuadPart / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *result = NUMBER_VAL(ts.tv_sec + ts.tv_nsec / 1e9);
#endif
}

static void osTime(int argCount, Value* args, Value* result) {
    (void)argCount; (void)args;
    *result = INTEGER_VAL((int64_t)time(NULL));
}

static void osGetenv(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = NIL_VAL; return; }
    const char* name = AS_STRING(args[0])->chars;
    const char* val = getenv(name);
    if (!val) { *result = NIL_VAL; return; }
    *result = OBJ_VAL(copyString(val, (int)strlen(val)));
}

static void osSetenv(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_STRING(args[0])) { *result = FALSE_VAL; return; }
    const char* name = AS_STRING(args[0])->chars;
    const char* val = IS_STRING(args[1]) ? AS_STRING(args[1])->chars : "";
#ifdef _WIN32
    int r = _putenv_s(name, val);
#else
    int r = setenv(name, val, 1);
#endif
    *result = BOOL_VAL(r == 0);
}

static void osExecute(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = NIL_VAL; return; }
#ifdef __EMSCRIPTEN__
    *result = NIL_VAL;
    return;
#else
    const char* cmd = AS_STRING(args[0])->chars;
    int r = system(cmd);
    *result = INTEGER_VAL(r);
#endif
}

static void osGetCwd(int argCount, Value* args, Value* result) {
    (void)argCount; (void)args;
    char buf[4096];
#ifdef _WIN32
    if (_getcwd(buf, sizeof(buf))) {
#else
    if (getcwd(buf, sizeof(buf))) {
#endif
        *result = OBJ_VAL(copyString(buf, (int)strlen(buf)));
    } else {
        *result = NIL_VAL;
    }
}

static void osSetCwd(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = FALSE_VAL; return; }
    const char* path = AS_STRING(args[0])->chars;
#ifdef _WIN32
    int r = _chdir(path);
#else
    int r = chdir(path);
#endif
    *result = BOOL_VAL(r == 0);
}

static ObjTable* osTable = NULL;

void initOSLibrary(void) {
    osTable = newTable();
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("os",2)), OBJ_VAL(osTable));

    #define ADD_OS(name, fn, arity) do { \
        ObjNative* nat = newNative(name, fn, arity); \
        Value key = OBJ_VAL((Obj*)copyString(name, strlen(name))); \
        tableSet(&osTable->fields, key, OBJ_VAL(nat)); \
    } while(0)

    ADD_OS("exit", osExit, 1);
    ADD_OS("sleep", osSleep, 1);
    ADD_OS("clock", osClock, 0);
    ADD_OS("time", osTime, 0);
    ADD_OS("getenv", osGetenv, 1);
    ADD_OS("setenv", osSetenv, 2);
    ADD_OS("execute", osExecute, 1);
    ADD_OS("getCwd", osGetCwd, 0);
    ADD_OS("getcwd", osGetCwd, 0);
    ADD_OS("setCwd", osSetCwd, 1);
    ADD_OS("setcwd", osSetCwd, 1);

    #undef ADD_OS

    ObjTable* argsTbl = newTable();
    tableSet(&osTable->fields, OBJ_VAL((Obj*)copyString("args",4)), OBJ_VAL(argsTbl));
}

void osSetArgs(int argc, const char* argv[]) {
    if (!osTable) return;
    Value argsKey = OBJ_VAL((Obj*)copyString("args",4));
    Value argsVal;
    if (!tableGet(&osTable->fields, argsKey, &argsVal) || !IS_TABLE(argsVal)) return;
    ObjTable* tbl = AS_TABLE(argsVal);
    for (int i=0;i<argc;i++) {
        Value s = OBJ_VAL(copyString(argv[i], (int)strlen(argv[i])));
        tableSet(&tbl->fields, INTEGER_VAL(i), s);
    }
    tableSet(&tbl->fields, OBJ_VAL((Obj*)copyString("n",1)), INTEGER_VAL(argc));
}
