#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libs/math.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"

static uint64_t rngState = 0;

static uint64_t nextRNG(void);

static double toNumber(Value v, bool* ok) {
    if (IS_INTEGER(v)) { *ok = true; return (double)AS_INTEGER(v); }
    if (IS_NUMBER(v))  { *ok = true; return AS_NUMBER_NC(v); }
    *ok = false;
    return 0.0;
}

static void mathSqrt(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(sqrt(x));
}

static void mathSin(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(sin(x));
}

static void mathCos(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(cos(x));
}

static void mathTan(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(tan(x));
}

static void mathAsin(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(asin(x));
}

static void mathAcos(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(acos(x));
}

static void mathAtan(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(atan(x));
}

static void mathAtan2(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    bool ok1, ok2; double y = toNumber(args[0], &ok1); double x = toNumber(args[1], &ok2);
    if (!ok1 || !ok2) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(atan2(y, x));
}

static void mathSinh(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(sinh(x));
}

static void mathCosh(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(cosh(x));
}

static void mathTanh(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(tanh(x));
}

static void mathAbs(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(fabs(x));
}

static void mathFloor(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(floor(x));
}

static void mathCeil(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(ceil(x));
}

static void mathExp(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(exp(x));
}

static void mathLog(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    if (argCount >= 2) {
        bool ok2; double base = toNumber(args[1], &ok2);
        if (!ok2) { *result = NIL_VAL; return; }
        *result = NUMBER_VAL(log(x) / log(base));
    } else {
        *result = NUMBER_VAL(log(x));
    }
}

static void mathLog10(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(log10(x));
}

static void mathPow(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    bool ok1, ok2; double x = toNumber(args[0], &ok1); double y = toNumber(args[1], &ok2);
    if (!ok1 || !ok2) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(pow(x, y));
}

static void mathMin(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double best = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    for (int i = 1; i < argCount; i++) {
        double v = toNumber(args[i], &ok);
        if (!ok) { *result = NIL_VAL; return; }
        if (v < best) best = v;
    }
    *result = NUMBER_VAL(best);
}

static void mathMax(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double best = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    for (int i = 1; i < argCount; i++) {
        double v = toNumber(args[i], &ok);
        if (!ok) { *result = NIL_VAL; return; }
        if (v > best) best = v;
    }
    *result = NUMBER_VAL(best);
}

static void mathDeg(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(x * (180.0 / 3.14159265358979323846));
}

static void mathRad(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(x * (3.14159265358979323846 / 180.0));
}

static void mathFmod(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    bool ok1, ok2; double x = toNumber(args[0], &ok1); double y = toNumber(args[1], &ok2);
    if (!ok1 || !ok2) { *result = NIL_VAL; return; }
    *result = NUMBER_VAL(fmod(x, y));
}

static void mathRandom(int argCount, Value* args, Value* result) {
    if (argCount == 0) {
        *result = NUMBER_VAL((double)nextRNG() / 18446744073709551616.0);
    } else if (argCount == 1) {
        bool ok; double n = toNumber(args[0], &ok);
        if (!ok) { *result = NIL_VAL; return; }
        int64_t m = (int64_t)n;
        *result = INTEGER_VAL((int64_t)(nextRNG() % m) + 1);
    } else {
        bool ok1, ok2; double lo = toNumber(args[0], &ok1); double hi = toNumber(args[1], &ok2);
        if (!ok1 || !ok2) { *result = NIL_VAL; return; }
        int64_t l = (int64_t)lo, h = (int64_t)hi;
        *result = INTEGER_VAL((int64_t)(nextRNG() % (h - l + 1)) + l);
    }
}

static void mathRandomseed(int argCount, Value* args, Value* result) {
    (void)result;
    if (argCount < 1) { *result = NIL_VAL; return; }
    bool ok; double x = toNumber(args[0], &ok);
    if (!ok) { *result = NIL_VAL; return; }
    rngState = (uint64_t)x;
}

static uint64_t nextRNG(void) {
    rngState = rngState * 6364136223846793005ULL + 1;
    return rngState;
}

static void mathType(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    if (IS_INTEGER(args[0])) {
        *result = OBJ_VAL(copyString("integer", 7));
    } else if (IS_NUMBER(args[0])) {
        *result = OBJ_VAL(copyString("float", 5));
    } else {
        *result = NIL_VAL;
    }
}

typedef struct {
    const char* name;
    NativeFn func;
    int arity;
} MathEntry;

static const MathEntry mathEntries[] = {
    {"sqrt",       mathSqrt,       1},
    {"sin",        mathSin,        1},
    {"cos",        mathCos,        1},
    {"tan",        mathTan,        1},
    {"asin",       mathAsin,       1},
    {"acos",       mathAcos,       1},
    {"atan",       mathAtan,       1},
    {"atan2",      mathAtan2,      2},
    {"sinh",       mathSinh,       1},
    {"cosh",       mathCosh,       1},
    {"tanh",       mathTanh,       1},
    {"abs",        mathAbs,        1},
    {"floor",      mathFloor,      1},
    {"ceil",       mathCeil,       1},
    {"exp",        mathExp,        1},
    {"log",        mathLog,        -1},
    {"log10",      mathLog10,      1},
    {"pow",        mathPow,        2},
    {"min",        mathMin,        -1},
    {"max",        mathMax,        -1},
    {"deg",        mathDeg,        1},
    {"rad",        mathRad,        1},
    {"fmod",       mathFmod,       2},
    {"random",     mathRandom,     -1},
    {"randomseed", mathRandomseed, 1},
    {"type",       mathType,       1},
};

static ObjNative* registerNative(const char* name, NativeFn func, int arity, ObjTable* table) {
    ObjNative* native = newNative(name, func, arity);
    Value key = OBJ_VAL((Obj*)copyString(name, strlen(name)));
    Value val = OBJ_VAL((Obj*)native);
    tableSet(&vm.globals, key, val);
    tableSet(&table->fields, key, val);
    return native;
}

void initNativeLibraries(void) {
    rngState = (uint64_t)time(NULL) * 6364136223846793005ULL;

    ObjTable* mathTable = newTable();
    Value mathKey = OBJ_VAL((Obj*)copyString("math", 4));
    tableSet(&vm.globals, mathKey, OBJ_VAL(mathTable));

    int count = sizeof(mathEntries) / sizeof(mathEntries[0]);
    for (int i = 0; i < count; i++) {
        if (mathEntries[i].arity >= 0 || mathEntries[i].arity == -1) {
            registerNative(mathEntries[i].name, mathEntries[i].func, mathEntries[i].arity, mathTable);
        }
    }

    tableSet(&mathTable->fields,
             OBJ_VAL((Obj*)copyString("pi", 2)),
             NUMBER_VAL(3.14159265358979323846));
    tableSet(&mathTable->fields,
             OBJ_VAL((Obj*)copyString("huge", 4)),
             NUMBER_VAL(INFINITY));
}
