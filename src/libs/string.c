#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/string.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"

#define STRBUF_MAX (1 << 24)

typedef struct {
    char* data;
    int length;
    int capacity;
    bool overflow;
} StrBuf;

static void bufInit(StrBuf* b) {
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
    b->overflow = false;
}

static void bufWrite(StrBuf* b, const char* src, int n) {
    if (n < 0 || b->overflow) return;
    if (b->length + n > STRBUF_MAX) {
        b->overflow = true;
        return;
    }
    if (b->length + n > b->capacity) {
        int newCap = b->capacity * 2 + 16;
        while (newCap < b->length + n) newCap *= 2;
        b->data = (char*)reallocate(b->data, (size_t)b->capacity, (size_t)newCap);
        b->capacity = newCap;
    }
    memcpy(b->data + b->length, src, (size_t)n);
    b->length += n;
}

static void bufAbort(StrBuf* b) {
    reallocate(b->data, (size_t)b->capacity, 0);
    b->data = NULL;
    b->length = 0;
    b->capacity = 0;
}

static Value bufFinish(StrBuf* b) {
    if (b->overflow) {
        bufAbort(b);
        return NIL_VAL;
    }
    Value result = OBJ_VAL(copyString(b->data, b->length));
    bufAbort(b);
    return result;
}

static bool toInt(Value v, int64_t* out) {
    if (IS_INTEGER(v)) { *out = AS_INTEGER(v); return true; }
    if (IS_NUMBER(v))  { *out = (int64_t)AS_NUMBER_NC(v); return true; }
    return false;
}

static ObjString* asString(Value v) {
    return IS_STRING(v) ? AS_STRING(v) : NULL;
}

static int64_t normIndex(int64_t i, int len) {
    if (i < 0) i = len + i + 1;
    if (i < 1) i = 1;
    if (i > len) i = len;
    return i;
}

static int64_t findIndex(ObjString* s, ObjString* needle, int64_t start) {
    if (needle->length == 0) {
        if (start < 1) start = 1;
        if (start > s->length + 1) return -1;
        return start;
    }
    if (needle->length > s->length) return -1;
    for (int64_t i = start - 1; i + needle->length <= s->length; i++) {
        if (memcmp(s->chars + i, needle->chars, (size_t)needle->length) == 0) return i + 1;
    }
    return -1;
}

static void strLen(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    *result = INTEGER_VAL(s->length);
}

static void strSub(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    int64_t start, end;
    if (!toInt(args[1], &start)) { *result = NIL_VAL; return; }
    if (argCount >= 3) {
        if (!toInt(args[2], &end)) { *result = NIL_VAL; return; }
    } else {
        end = s->length;
    }
    int len = s->length;
    if (len == 0) { *result = OBJ_VAL(copyString("", 0)); return; }
    start = normIndex(start, len);
    end = normIndex(end, len);
    if (start > end) { *result = OBJ_VAL(copyString("", 0)); return; }
    *result = OBJ_VAL(copyString(s->chars + (start - 1), (int)(end - start + 1)));
}

static void strUpper(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    StrBuf b; bufInit(&b);
    for (int i = 0; i < s->length; i++) {
        char c = (char)toupper((unsigned char)s->chars[i]);
        bufWrite(&b, &c, 1);
    }
    *result = bufFinish(&b);
}

static void strLower(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    StrBuf b; bufInit(&b);
    for (int i = 0; i < s->length; i++) {
        char c = (char)tolower((unsigned char)s->chars[i]);
        bufWrite(&b, &c, 1);
    }
    *result = bufFinish(&b);
}

static void strTrim(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    int start = 0, end = s->length;
    while (start < end && isspace((unsigned char)s->chars[start])) start++;
    while (end > start && isspace((unsigned char)s->chars[end - 1])) end--;
    *result = OBJ_VAL(copyString(s->chars + start, end - start));
}

static void tableArraySet(ObjTable* table, int index, Value value) {
    if (table->arrayCapacity < index) {
        int newCap = table->arrayCapacity * 2 + 4;
        while (newCap < index) newCap *= 2;
        table->array = (Value*)reallocate(table->array,
                                          sizeof(Value) * (size_t)table->arrayCapacity,
                                          sizeof(Value) * (size_t)newCap);
        for (int i = table->arrayCapacity; i < newCap; i++) table->array[i] = NIL_VAL;
        table->arrayCapacity = newCap;
    }
    table->array[index - 1] = value;
}

static void strSplit(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    ObjString* sep = NULL;
    if (argCount >= 2) {
        if (!IS_NIL(args[1])) {
            sep = asString(args[1]);
            if (!sep) { *result = NIL_VAL; return; }
        }
    }
    ObjTable* table = newTable();
    int len = s->length;
    if (sep == NULL || sep->length == 0) {
        for (int i = 0; i < len; i++) {
            tableArraySet(table, i + 1, OBJ_VAL(copyString(s->chars + i, 1)));
        }
    } else {
        int count = 0;
        int pos = 0;
        while (pos <= len) {
            int next = -1;
            for (int i = pos; i + sep->length <= len; i++) {
                if (memcmp(s->chars + i, sep->chars, (size_t)sep->length) == 0) { next = i; break; }
            }
            int pieceEnd = (next == -1) ? len : next;
            count++;
            tableArraySet(table, count, OBJ_VAL(copyString(s->chars + pos, pieceEnd - pos)));
            if (next == -1) break;
            pos = next + sep->length;
        }
    }
    *result = OBJ_VAL(table);
}

static void strFind(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    ObjString* needle = asString(args[1]);
    if (!s || !needle) { *result = NIL_VAL; return; }
    int64_t start = 1;
    if (argCount >= 3) {
        if (!toInt(args[2], &start)) { *result = NIL_VAL; return; }
    }
    start = normIndex(start, s->length);
    if (start < 1) start = 1;
    int64_t pos = findIndex(s, needle, start);
    if (pos == -1) { *result = NIL_VAL; return; }
    *result = INTEGER_VAL(pos);
}

static void strContains(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    ObjString* needle = asString(args[1]);
    if (!s || !needle) { *result = NIL_VAL; return; }
    if (needle->length == 0) { *result = TRUE_VAL; return; }
    *result = BOOL_VAL(findIndex(s, needle, 1) != -1);
}

static void strStartsWith(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    ObjString* prefix = asString(args[1]);
    if (!s || !prefix) { *result = NIL_VAL; return; }
    *result = BOOL_VAL(prefix->length <= s->length &&
                       memcmp(s->chars, prefix->chars, (size_t)prefix->length) == 0);
}

static void strEndsWith(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    ObjString* suffix = asString(args[1]);
    if (!s || !suffix) { *result = NIL_VAL; return; }
    *result = BOOL_VAL(suffix->length <= s->length &&
                       memcmp(s->chars + s->length - suffix->length, suffix->chars, (size_t)suffix->length) == 0);
}

static void strReplace(int argCount, Value* args, Value* result) {
    if (argCount < 3) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    ObjString* from = asString(args[1]);
    ObjString* to = asString(args[2]);
    if (!s || !from || !to) { *result = NIL_VAL; return; }
    if (from->length == 0) { *result = OBJ_VAL(copyString(s->chars, s->length)); return; }
    StrBuf b; bufInit(&b);
    int pos = 0;
    while (pos <= s->length) {
        int64_t hit = -1;
        for (int64_t i = pos; i + from->length <= s->length; i++) {
            if (memcmp(s->chars + i, from->chars, (size_t)from->length) == 0) { hit = i; break; }
        }
        if (hit == -1) {
            bufWrite(&b, s->chars + pos, s->length - pos);
            break;
        }
        bufWrite(&b, s->chars + pos, (int)(hit - pos));
        bufWrite(&b, to->chars, to->length);
        pos = (int)hit + from->length;
    }
    *result = bufFinish(&b);
}

static void strRepeat(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    int64_t n;
    if (!toInt(args[1], &n)) { *result = NIL_VAL; return; }
    if (n <= 0) { *result = OBJ_VAL(copyString("", 0)); return; }
    if ((int64_t)s->length * n > STRBUF_MAX) { *result = NIL_VAL; return; }
    StrBuf b; bufInit(&b);
    for (int64_t i = 0; i < n; i++) bufWrite(&b, s->chars, s->length);
    *result = bufFinish(&b);
}

static void strReverse(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    StrBuf b; bufInit(&b);
    for (int i = s->length - 1; i >= 0; i--) bufWrite(&b, s->chars + i, 1);
    *result = bufFinish(&b);
}

static void strFormat(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    ObjString* fmt = asString(args[0]);
    if (!fmt) { *result = NIL_VAL; return; }
    int argIndex = 1;
    StrBuf b; bufInit(&b);
    for (int i = 0; i < fmt->length; i++) {
        if (fmt->chars[i] == '%' && i + 1 < fmt->length) {
            char spec = fmt->chars[i + 1];
            if (spec == '%') {
                bufWrite(&b, "%", 1);
                i++;
            } else if (spec == 's') {
                i++;
                if (argIndex >= argCount) { bufAbort(&b); *result = NIL_VAL; return; }
                ObjString* a = asString(args[argIndex]);
                if (!a) { bufAbort(&b); *result = NIL_VAL; return; }
                argIndex++;
                bufWrite(&b, a->chars, a->length);
            } else if (spec == 'd' || spec == 'i') {
                i++;
                if (argIndex >= argCount || !IS_INTEGER(args[argIndex])) { bufAbort(&b); *result = NIL_VAL; return; }
                char tmp[64];
                int l = snprintf(tmp, sizeof(tmp), "%lld", (long long)AS_INTEGER(args[argIndex]));
                argIndex++;
                bufWrite(&b, tmp, l);
            } else if (spec == 'f' || spec == 'g' || spec == 'e') {
                i++;
                if (argIndex >= argCount) { bufAbort(&b); *result = NIL_VAL; return; }
                Value v = args[argIndex];
                if (!IS_INTEGER(v) && !IS_NUMBER(v)) { bufAbort(&b); *result = NIL_VAL; return; }
                argIndex++;
                char tmp[64];
                int l;
                if (spec == 'f') l = snprintf(tmp, sizeof(tmp), "%f", AS_NUMBER(v));
                else if (spec == 'e') l = snprintf(tmp, sizeof(tmp), "%e", AS_NUMBER(v));
                else l = snprintf(tmp, sizeof(tmp), "%g", AS_NUMBER(v));
                bufWrite(&b, tmp, l);
            } else {
                bufWrite(&b, fmt->chars + i, 2);
                i++;
            }
        } else {
            bufWrite(&b, fmt->chars + i, 1);
        }
    }
    *result = bufFinish(&b);
}

static void strCharCode(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = NIL_VAL; return; }
    ObjString* s = asString(args[0]);
    if (!s) { *result = NIL_VAL; return; }
    if (s->length == 0) { *result = NIL_VAL; return; }
    int64_t i;
    if (!toInt(args[1], &i)) { *result = NIL_VAL; return; }
    i = normIndex(i, s->length);
    if (i < 1) { *result = NIL_VAL; return; }
    *result = INTEGER_VAL((unsigned char)s->chars[i - 1]);
}

static void strFromCharCode(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    StrBuf b; bufInit(&b);
    for (int i = 0; i < argCount; i++) {
        if (!IS_INTEGER(args[i])) { bufAbort(&b); *result = NIL_VAL; return; }
        int64_t c = AS_INTEGER(args[i]);
        if (c < 0 || c > 255) { bufAbort(&b); *result = NIL_VAL; return; }
        char ch = (char)c;
        bufWrite(&b, &ch, 1);
    }
    *result = bufFinish(&b);
}

static void strToString(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    Value v = args[0];
    if (IS_STRING(v)) { *result = v; return; }
    if (IS_NIL(v))   { *result = OBJ_VAL(copyString("nil", 3)); return; }
    if (IS_BOOL(v))  { *result = OBJ_VAL(copyString(AS_BOOL(v) ? "true" : "false", AS_BOOL(v) ? 4 : 5)); return; }
    if (IS_TABLE(v)) { *result = OBJ_VAL(copyString("<table>", 7)); return; }
    if (IS_CLOSURE(v) || IS_NATIVE(v) || IS_FUNCTION(v)) {
        *result = OBJ_VAL(copyString("<function>", 10));
        return;
    }
    if (IS_INTEGER(v) || IS_NUMBER(v)) {
        char buf[64];
        int len;
        if (IS_INTEGER(v)) {
            len = snprintf(buf, sizeof(buf), "%lld", (long long)AS_INTEGER(v));
        } else {
            double d = AS_NUMBER_NC(v);
            if (d == (double)(int64_t)d) len = snprintf(buf, sizeof(buf), "%.0f", d);
            else len = snprintf(buf, sizeof(buf), "%g", d);
        }
        *result = OBJ_VAL(copyString(buf, len));
        return;
    }
    *result = NIL_VAL;
}

static void strToNumber(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    Value v = args[0];
    if (IS_INTEGER(v) || IS_NUMBER(v)) { *result = v; return; }
    if (!IS_STRING(v)) { *result = NIL_VAL; return; }
    ObjString* s = AS_STRING(v);
    char* buf = (char*)reallocate(NULL, 0, (size_t)s->length + 1);
    memcpy(buf, s->chars, (size_t)s->length);
    buf[s->length] = '\0';
    char* end = NULL;
    long long ll = strtoll(buf, &end, 10);
    if (end != buf && *end == '\0') {
        reallocate(buf, (size_t)s->length + 1, 0);
        *result = INTEGER_VAL((int64_t)ll);
        return;
    }
    double d = strtod(buf, &end);
    if (end != buf && *end == '\0') {
        reallocate(buf, (size_t)s->length + 1, 0);
        *result = NUMBER_VAL(d);
        return;
    }
    reallocate(buf, (size_t)s->length + 1, 0);
    *result = NIL_VAL;
}

static void strType(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    Value v = args[0];
    const char* t;
    if (IS_NIL(v))                      t = "nil";
    else if (IS_BOOL(v))                t = "boolean";
    else if (IS_INTEGER(v) || IS_NUMBER(v)) t = "number";
    else if (IS_STRING(v))              t = "string";
    else if (IS_TABLE(v))               t = "table";
    else if (IS_CLOSURE(v) || IS_NATIVE(v) || IS_FUNCTION(v)) t = "function";
    else { *result = NIL_VAL; return; }
    *result = OBJ_VAL(copyString(t, (int)strlen(t)));
}

typedef struct {
    const char* name;
    NativeFn func;
    int arity;
} StringEntry;

static const StringEntry stringEntries[] = {
    {"len",          strLen,          1},
    {"sub",          strSub,          -1},
    {"upper",        strUpper,        1},
    {"lower",        strLower,        1},
    {"trim",         strTrim,         1},
    {"split",        strSplit,        -1},
    {"find",         strFind,         -1},
    {"contains",     strContains,     2},
    {"startsWith",   strStartsWith,   2},
    {"endsWith",     strEndsWith,     2},
    {"replace",      strReplace,      3},
    {"repeat",       strRepeat,       2},
    {"rep",          strRepeat,       2},
    {"reverse",      strReverse,      1},
    {"format",       strFormat,       -1},
    {"charCode",     strCharCode,     2},
    {"byte",         strCharCode,     2},
    {"fromCharCode", strFromCharCode, -1},
    {"char",         strFromCharCode, -1},
};

void initStringLibrary(void) {
    ObjTable* stringTable = newTable();
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("string", 6)), OBJ_VAL(stringTable));

    int count = sizeof(stringEntries) / sizeof(stringEntries[0]);
    for (int i = 0; i < count; i++) {
        ObjNative* native = newNative(stringEntries[i].name, stringEntries[i].func, stringEntries[i].arity);
        Value key = OBJ_VAL((Obj*)copyString(stringEntries[i].name, (int)strlen(stringEntries[i].name)));
        tableSet(&vm.globals, key, OBJ_VAL((Obj*)native));
        tableSet(&stringTable->fields, key, OBJ_VAL((Obj*)native));
    }

    ObjNative* tostringNative = newNative("tostring", strToString, 1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("tostring", 8)), OBJ_VAL((Obj*)tostringNative));
    ObjNative* tonumberNative = newNative("tonumber", strToNumber, 1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("tonumber", 8)), OBJ_VAL((Obj*)tonumberNative));
    ObjNative* typeNative = newNative("type", strType, 1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("type", 4)), OBJ_VAL((Obj*)typeNative));
}