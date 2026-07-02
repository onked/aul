#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "object.h"
#include "table.h"

static void growGrayStack() {
    if (vm.grayCount >= vm.grayCapacity) {
        vm.grayCapacity = vm.grayCapacity == 0 ? 256 : vm.grayCapacity * 2;
        vm.grayStack = reallocate(vm.grayStack, sizeof(Obj*) * (vm.grayCount),
                                  sizeof(Obj*) * vm.grayCapacity);
    }
}

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->marked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
#endif

    object->marked = true;
    growGrayStack();
    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
    if (!IS_OBJ(value)) return;
    markObject(AS_OBJ(value));
}

void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
#endif

    switch (object->type) {
    case OBJ_STRING:
        break;
    case OBJ_FUNCTION: {
        ObjFunction* function = (ObjFunction*)object;
        markObject((Obj*)function->name);
        markArray(&function->chunk.constants);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)object;
        markObject((Obj*)closure->function);
        for (int i = 0; i < closure->upvalueCount; i++) {
            if (closure->upvalues[i]) {
                markObject((Obj*)closure->upvalues[i]);
            } else {
                markValue(closure->readonlyValues[i]);
            }
        }
        break;
    }
    case OBJ_UPVALUE:
        // Closed upvalues hold a Value directly
        markValue(((ObjUpvalue*)object)->closed);
        break;
    case OBJ_TABLE: {
        ObjTable* table = (ObjTable*)object;
        if (table->metatable != NULL) {
            markObject((Obj*)table->metatable);
        }
        for (int i = 0; i < table->arrayCapacity; i++) {
            markValue(table->array[i]);
        }
        // Mark all hash part entries (keys and values)
        for (int i = 0; i < table->fields.capacity; i++) {
            Entry* entry = &table->fields.entries[i];
            if (!IS_NIL(entry->key)) {
                markValue(entry->key);
                markValue(entry->value);
            }
        }
        // Mark cached metamethod values
        markValue(table->cachedIndex);
        markValue(table->cachedNewIndex);
        markValue(table->cachedCall);
        markValue(table->cachedLen);
        break;
    }
    }
}

// Mark roots: in a register VM we scan each frame's register window,
// not a single stack pointer
void markRoots() {
    // Scan each call frame's register window
    for (int i = 0; i < vm.frameCount; i++) {
        CallFrame* frame = &vm.frames[i];
        markObject((Obj*)frame->closure);
        int regCount = frame->closure->function->maxRegs;
        if (regCount == 0) regCount = 1; // safety: always scan at least slot 0
        for (int r = 0; r < regCount; r++) {
            markValue(frame->slots[r]);
        }
    }

    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    for (int i = 0; i < vm.globals.capacity; i++) {
        Entry* entry = &vm.globals.entries[i];
        if (!IS_NIL(entry->key)) {
            markValue(entry->key);
            markValue(entry->value);
        }
    }

    markObject((Obj*)vm.mmIndex);
    markObject((Obj*)vm.mmNewIndex);
    markObject((Obj*)vm.mmCall);
    markObject((Obj*)vm.mmLen);

    for (int i = 0; i < IC_SIZE; i++) {
        InlineCache* ic = &vm.inlineCache[i];
        if (ic->valid) {
            markValue(ic->table);
            markValue(ic->key);
            markValue(ic->result);
        }
    }

    for (int i = 0; i < vm.strings.capacity; i++) {
        Entry* entry = &vm.strings.entries[i];
        if (!IS_NIL(entry->key)) {
            markValue(entry->key);
        }
    }
}

void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    while (object != NULL) {
        if (object->marked) {
            object->marked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        }
    }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

    markRoots();

#ifdef DEBUG_LOG_GC
    // Capture after markRoots (which may allocate gray stack)
    size_t before = vm.bytesAllocated;
#endif

    traceReferences();
    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

    for (int i = 0; i < IC_SIZE; i++) {
        vm.inlineCache[i].valid = false;
    }

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("-- collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}

void freeObject(Obj* object) {
    size_t objSize = 0;
    switch (object->type) {
    case OBJ_STRING:  objSize = sizeof(ObjString);  break;
    case OBJ_FUNCTION: objSize = sizeof(ObjFunction); break;
    case OBJ_CLOSURE:  objSize = sizeof(ObjClosure);  break;
    case OBJ_UPVALUE:  objSize = sizeof(ObjUpvalue);  break;
    case OBJ_TABLE:    objSize = sizeof(ObjTable);    break;
    }

#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
    case OBJ_STRING: {
        ObjString* string = (ObjString*)object;
        reallocate(string->chars, string->length + 1, 0);
        break;
    }
    case OBJ_FUNCTION: {
        ObjFunction* function = (ObjFunction*)object;
        freeChunk(&function->chunk);
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)object;
        reallocate(closure->upvalues, sizeof(ObjUpvalue*) * closure->upvalueCount, 0);
        reallocate(closure->readonlyValues, sizeof(Value) * closure->upvalueCount, 0);
        break;
    }
    case OBJ_UPVALUE: {
        break;
    }
    case OBJ_TABLE: {
        ObjTable* table = (ObjTable*)object;
        if (table->array != NULL) {
            reallocate(table->array, sizeof(Value) * table->arrayCapacity, 0);
        }
        freeTable(&table->fields);
        break;
    }
    }

    reallocate(object, objSize, 0);
}