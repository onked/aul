#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "object.h"
#include "value.h"
#include "vm.h"
#include "chunk.h"
#include "memory.h"

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->marked = vm.currentWhite;
    object->next = NULL;

    if (vm.gcPhase == GC_PHASE_SWEEP) {
        object->next = vm.newObjects;
        vm.newObjects = object;
    } else {
        object->next = vm.objects;
        vm.objects = object;
        if (vm.gcPhase == GC_PHASE_MARK || vm.gcPhase == GC_PHASE_ATOMIC) {
            markObject(object);
        }
    }

    return object;
}

ObjFunction* newFunction() {
    ObjFunction* function = (ObjFunction*)allocateObject(sizeof(ObjFunction), OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->maxRegs = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjNative* newNative(const char* name, NativeFn function, int arity) {
    ObjNative* native = (ObjNative*)allocateObject(sizeof(ObjNative), OBJ_NATIVE);
    native->function = function;
    native->name = copyString(name, strlen(name));
    native->arity = arity;
    return native;
}

ObjClosure* newClosure(ObjFunction* function) {
    int count = function->upvalueCount;
    ObjUpvalue** upvalues = (ObjUpvalue**)reallocate(NULL, 0, sizeof(ObjUpvalue*) * count);
    for (int i = 0; i < count; i++) {
        upvalues[i] = NULL;
    }
    Value* readonlyValues = (Value*)reallocate(NULL, 0, sizeof(Value) * count);
    ObjClosure* closure = (ObjClosure*)allocateObject(sizeof(ObjClosure), OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->readonlyValues = readonlyValues;
    closure->upvalueCount = count;
    return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = (ObjUpvalue*)allocateObject(sizeof(ObjUpvalue), OBJ_UPVALUE);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

ObjTable* newTable() {
    ObjTable* table = (ObjTable*)allocateObject(sizeof(ObjTable), OBJ_TABLE);
    table->arrayCapacity = 0;
    table->array = NULL;
    table->inlineCount = 0;
    for (int i = 0; i < TABLE_INLINE_CAPACITY; i++) {
        table->inlineKeys[i] = NIL_VAL;
        table->inlineVals[i] = NIL_VAL;
    }
    initTable(&table->fields);
    table->metatable = NULL;
    table->writeGen = 0;
    table->metaGen = 0;
    table->cachedIndex = NIL_VAL;
    table->cachedNewIndex = NIL_VAL;
    table->cachedCall = NIL_VAL;
    table->cachedLen = NIL_VAL;
    return table;
}

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    string->hashValid = true;
    return string;
}

uint32_t stringHash(ObjString* string) {
    if (string->hashValid) return string->hash;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < string->length; i++) {
        hash ^= (uint8_t)string->chars[i];
        hash *= 16777619;
    }
    string->hash = hash;
    string->hashValid = true;
    return hash;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        reallocate(chars, length + 1, 0);
        return interned;
    }
    ObjString* string = allocateString(chars, length, hash);
    tableSet(&vm.strings, OBJ_VAL((Obj*)string), NIL_VAL);
    return string;
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char* heapChars = (char*)reallocate(NULL, 0, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    ObjString* string = allocateString(heapChars, length, hash);
    tableSet(&vm.strings, OBJ_VAL((Obj*)string), NIL_VAL);
    return string;
}

ObjString* rawString(char* chars, int length, uint32_t hash) {
    ObjString* string = allocateString(chars, length, hash);
    string->hashValid = false;
    return string;
}