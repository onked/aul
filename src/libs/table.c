#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/table.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"

#define TABLE_BUF_MAX (1 << 24)

static void tableWriteBarrier(Value value) {
    if (vm.gcPhase == GC_PHASE_MARK || vm.gcPhase == GC_PHASE_ATOMIC) {
        if (IS_OBJ(value)) markObject(AS_OBJ(value));
    }
}

static bool toInt(Value v, int64_t* out) {
    if (IS_INTEGER(v)) { *out = AS_INTEGER(v); return true; }
    if (IS_NUMBER(v))  { *out = (int64_t)AS_NUMBER_NC(v); return true; }
    return false;
}

static ObjTable* toTable(Value v) {
    return IS_TABLE(v) ? AS_TABLE(v) : NULL;
}

static int tableLength(ObjTable* t) {
    int len = t->arrayCapacity;
    while (len > 0 && IS_NIL(t->array[len - 1])) len--;
    return len;
}

static bool tableGetIndex(ObjTable* t, int64_t index, Value* out) {
    if (index >= 1 && index <= (int64_t)t->arrayCapacity) {
        Value v = t->array[index - 1];
        if (!IS_NIL(v)) { *out = v; return true; }
    }
    return objTableGet(t, INTEGER_VAL(index), out);
}

static bool tableSetIndex(ObjTable* t, int64_t index, Value value) {
    if (index < 1) return false;
    t->writeGen++;
    if (index <= (int64_t)t->arrayCapacity) {
        tableWriteBarrier(value);
        t->array[index - 1] = value;
    } else {
        int64_t growCap = (int64_t)t->arrayCapacity * 2 + 4;
        if (t->arrayCapacity < 64 || index <= growCap) {
            int newCap = t->arrayCapacity * 2 + 4;
            if (newCap < index) newCap = (int)index;
            t->array = (Value*)reallocate(t->array,
                                          sizeof(Value) * (size_t)t->arrayCapacity,
                                          sizeof(Value) * (size_t)newCap);
            for (int i = t->arrayCapacity; i < newCap; i++) t->array[i] = NIL_VAL;
            t->arrayCapacity = newCap;
            tableWriteBarrier(value);
            t->array[index - 1] = value;
        } else {
            tableSet(&t->fields, INTEGER_VAL(index), value);
        }
    }
    return true;
}

static void tblInsert(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    int len = tableLength(t);
    int64_t pos = len + 1;
    if (argCount >= 3) {
        if (!toInt(args[1], &pos)) { *result = NIL_VAL; return; }
        if (pos < 1) { *result = NIL_VAL; return; }
        if (pos > len + 1) pos = len + 1;
    }
    for (int64_t i = (int64_t)len; i >= pos; i--) {
        Value v;
        if (!tableGetIndex(t, i, &v)) v = NIL_VAL;
        tableSetIndex(t, i + 1, v);
    }
    tableSetIndex(t, pos, args[argCount - 1]);
    *result = NIL_VAL;
}

static void tblRemove(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    int len = tableLength(t);
    int64_t pos = len;
    if (argCount >= 2) {
        if (!toInt(args[1], &pos)) { *result = NIL_VAL; return; }
        if (pos < 1) { *result = NIL_VAL; return; }
        if (pos > len) pos = len;
    }
    Value out = NIL_VAL;
    tableGetIndex(t, pos, &out);
    for (int64_t i = pos; i < (int64_t)len; i++) {
        Value v;
        if (!tableGetIndex(t, i + 1, &v)) v = NIL_VAL;
        tableSetIndex(t, i, v);
    }
    if (len > 0) {
        t->writeGen++;
        t->array[len - 1] = NIL_VAL;
    }
    *result = out;
}

static void tblClear(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    t->writeGen++;
    for (int i = 0; i < t->arrayCapacity; i++) t->array[i] = NIL_VAL;
    if (t->inlineCount >= 0) {
        for (int i = 0; i < t->inlineCount; i++) {
            t->inlineKeys[i] = NIL_VAL;
            t->inlineVals[i] = NIL_VAL;
        }
        t->inlineCount = 0;
    } else {
        freeTable(&t->fields);
        initTable(&t->fields);
    }
    *result = NIL_VAL;
}

static void tblFind(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    int len = tableLength(t);
    for (int i = 1; i <= len; i++) {
        Value v;
        if (tableGetIndex(t, i, &v) && valuesEqual(v, args[1])) {
            *result = INTEGER_VAL(i);
            return;
        }
    }
    *result = NIL_VAL;
}

static void tblMaxn(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    int64_t max = 0;
    for (int64_t i = 1; i <= (int64_t)t->arrayCapacity; i++) {
        if (!IS_NIL(t->array[i - 1])) max = i;
    }
    *result = INTEGER_VAL(max);
}

static void tblGetn(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    *result = INTEGER_VAL(tableLength(t));
}

static void tblConcat(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    ObjString* sep = NULL;
    if (argCount >= 2) {
        if (IS_STRING(args[1])) sep = AS_STRING(args[1]);
        else if (!IS_NIL(args[1])) { *result = NIL_VAL; return; }
    }
    int len = tableLength(t);
    size_t total = 0;
    for (int i = 1; i <= len; i++) {
        Value v;
        if (!tableGetIndex(t, i, &v) || !IS_STRING(v)) { *result = NIL_VAL; return; }
        total += AS_STRING(v)->length;
    }
    size_t sepLen = (sep == NULL) ? 0 : (size_t)sep->length;
    size_t sepCount = (len > 0) ? (size_t)(len - 1) : 0;
    if (total + sepLen * sepCount > TABLE_BUF_MAX) { *result = NIL_VAL; return; }
    size_t cap = total + sepLen * sepCount;
    char* buf = (char*)reallocate(NULL, 0, cap + 1);
    size_t off = 0;
    for (int i = 1; i <= len; i++) {
        Value v;
        tableGetIndex(t, i, &v);
        ObjString* s = AS_STRING(v);
        memcpy(buf + off, s->chars, (size_t)s->length);
        off += (size_t)s->length;
        if (i < len && sepLen > 0) {
            memcpy(buf + off, sep->chars, sepLen);
            off += sepLen;
        }
    }
    buf[off] = '\0';
    ObjString* joined = copyString(buf, (int)off);
    reallocate(buf, cap + 1, 0);
    *result = OBJ_VAL(joined);
}

static int compareValues(Value a, Value b) {
    bool aNum = IS_INTEGER(a) || IS_NUMBER(a);
    bool bNum = IS_INTEGER(b) || IS_NUMBER(b);
    if (aNum && bNum) {
        double x = AS_NUMBER(a), y = AS_NUMBER(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* x = AS_STRING(a);
        ObjString* y = AS_STRING(b);
        int l = x->length < y->length ? x->length : y->length;
        int c = memcmp(x->chars, y->chars, (size_t)l);
        if (c != 0) return c < 0 ? -1 : 1;
        return x->length < y->length ? -1 : (x->length > y->length ? 1 : 0);
    }
    return 0;
}

static int qsortCompare(const void* pa, const void* pb) {
    return compareValues(*(const Value*)pa, *(const Value*)pb);
}

static void tblSort(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjTable* t = toTable(args[0]);
    if (!t) { *result = NIL_VAL; return; }
    if (argCount >= 2 && !IS_NIL(args[1])) {
        *result = NIL_VAL;
        return;
    }
    int len = tableLength(t);
    if (len <= 1) { *result = NIL_VAL; return; }
    Value* buf = (Value*)reallocate(NULL, 0, sizeof(Value) * (size_t)len);
    for (int i = 1; i <= len; i++) {
        Value v;
        tableGetIndex(t, i, &v);
        buf[i - 1] = v;
    }
    qsort(buf, (size_t)len, sizeof(Value), qsortCompare);
    t->writeGen++;
    for (int i = 1; i <= len; i++) {
        tableWriteBarrier(buf[i - 1]);
        t->array[i - 1] = buf[i - 1];
    }
    reallocate(buf, sizeof(Value) * (size_t)len, 0);
    *result = NIL_VAL;
}

static void tblPack(int argCount, Value* args, Value* result) {
    ObjTable* t = newTable();
    for (int i = 0; i < argCount; i++) {
        tableSetIndex(t, i + 1, args[i]);
    }
    if (argCount > 0) {
        tableSet(&t->fields, OBJ_VAL((Obj*)copyString("n", 1)), INTEGER_VAL(argCount));
    }
    *result = OBJ_VAL(t);
}

static void tblCreate(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    int64_t count;
    if (!toInt(args[0], &count)) { *result = NIL_VAL; return; }
    if (count < 0 || count > TABLE_BUF_MAX) { *result = NIL_VAL; return; }
    Value fill = NIL_VAL;
    if (argCount >= 2) fill = args[1];
    ObjTable* t = newTable();
    if (count > (int64_t)t->arrayCapacity) {
        int newCap = (int)count;
        t->array = (Value*)reallocate(t->array, 0, sizeof(Value) * (size_t)newCap);
        for (int i = 0; i < newCap; i++) t->array[i] = NIL_VAL;
        t->arrayCapacity = newCap;
    }
    t->writeGen++;
    if (!IS_NIL(fill)) {
        for (int64_t i = 1; i <= count; i++) {
            tableWriteBarrier(fill);
            t->array[i - 1] = fill;
        }
    }
    tableSet(&t->fields, OBJ_VAL((Obj*)copyString("count", 5)), INTEGER_VAL(count));
    *result = OBJ_VAL(t);
}

typedef struct {
    const char* name;
    NativeFn func;
    int arity;
} TableEntry;

static const TableEntry tableEntries[] = {
    {"insert", tblInsert, -1},
    {"remove", tblRemove, -1},
    {"clear",  tblClear,  1},
    {"find",   tblFind,   2},
    {"maxn",   tblMaxn,   1},
    {"getn",   tblGetn,   1},
    {"concat", tblConcat, -1},
    {"sort",   tblSort,   -1},
    {"pack",   tblPack,   -1},
    {"create", tblCreate, -1},
};

void initTableLibrary(void) {
    ObjTable* tableTable = newTable();
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("table", 5)), OBJ_VAL(tableTable));

    int count = sizeof(tableEntries) / sizeof(tableEntries[0]);
    for (int i = 0; i < count; i++) {
        ObjNative* native = newNative(tableEntries[i].name, tableEntries[i].func, tableEntries[i].arity);
        Value key = OBJ_VAL((Obj*)copyString(tableEntries[i].name, (int)strlen(tableEntries[i].name)));
        tableSet(&tableTable->fields, key, OBJ_VAL((Obj*)native));
    }
}