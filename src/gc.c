#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "object.h"
#include "table.h"

void growGrayStack() {
    if (vm.grayCount >= vm.grayCapacity) {
        vm.grayCapacity = vm.grayCapacity == 0 ? 256 : vm.grayCapacity * 2;
        vm.grayStack = reallocate(vm.grayStack, sizeof(Obj*) * (vm.grayCount),
                                  sizeof(Obj*) * vm.grayCapacity);
    }
}

void markObject(Obj* object) {
    if (object == NULL) return;
    if ((object->marked & GC_COLOR_MASK) != vm.currentWhite) return;

    object->marked |= GC_COLOR_GRAY;
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
    object->marked = (object->marked & ~GC_COLOR_MASK) | GC_COLOR_BLACK;

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
        if (table->inlineCount > 0) {
            for (int i = 0; i < table->inlineCount; i++) {
                if (!IS_NIL(table->inlineKeys[i])) {
                    markValue(table->inlineKeys[i]);
                }
            }
        }
        for (int i = 0; i < table->fields.capacity; i++) {
            Entry* entry = &table->fields.entries[i];
            if (!IS_NIL(entry->key)) {
                markValue(entry->key);
                markValue(entry->value);
            }
        }
        markValue(table->cachedIndex);
        markValue(table->cachedNewIndex);
        markValue(table->cachedCall);
        markValue(table->cachedLen);
        break;
    }
    case OBJ_NATIVE: {
        ObjNative* native = (ObjNative*)object;
        markObject((Obj*)native->name);
        break;
    }

    }
}

void barrierBack(Obj* obj) {
    if (obj == NULL || obj->type != OBJ_TABLE || (obj->marked & GC_COLOR_MASK) != GC_COLOR_BLACK) return;
    obj->marked |= GC_COLOR_GRAY;
    ObjTable* t = (ObjTable*)obj;
    t->gclist = vm.grayagain;
    vm.grayagain = t;
}

void markRoots() {
    for (int i = 0; i < vm.frameCount; i++) {
        CallFrame* frame = &vm.frames[i];
        markObject((Obj*)frame->closure);
        int regCount = frame->closure->function->maxRegs;
        if (regCount == 0) regCount = 1;
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
    markObject((Obj*)vm.mmAdd);
    markObject((Obj*)vm.mmSub);
    markObject((Obj*)vm.mmMul);
    markObject((Obj*)vm.mmDiv);
    if (vm.openString != NIL_VAL) markValue(vm.openString);

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
        if ((object->marked & GC_COLOR_MASK) == vm.otherWhite) {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }
            freeObject(unreached);
        } else {
            object->marked = (object->marked & ~GC_COLOR_MASK) | vm.currentWhite;
            previous = object;
            object = object->next;
        }
    }
}

void collectGarbage() {
    markRoots();
    traceReferences();

    while (vm.grayagain != NULL) {
        ObjTable* t = vm.grayagain;
        vm.grayagain = t->gclist;
        t->gclist = NULL;
        growGrayStack();
        vm.grayStack[vm.grayCount++] = (Obj*)t;
    }
    traceReferences();

    vm.otherWhite = vm.currentWhite;
    vm.currentWhite ^= 1;

    sweep();

    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    vm.gcPhase = GC_PHASE_IDLE;

    for (Obj* o = vm.objects; o != NULL; o = o->next) {
        if (o->type == OBJ_FUNCTION) {
            Chunk* c = &((ObjFunction*)o)->chunk;
            for (int i = 0; i < c->count; i++) {
                c->caches[i].valid = false;
            }
        }
    }
}

void freeObject(Obj* object) {
    size_t objSize = 0;
    switch (object->type) {
    case OBJ_STRING:  objSize = sizeof(ObjString);  break;
    case OBJ_FUNCTION: objSize = sizeof(ObjFunction); break;
    case OBJ_CLOSURE:  objSize = sizeof(ObjClosure);  break;
    case OBJ_UPVALUE:  objSize = sizeof(ObjUpvalue);  break;
    case OBJ_TABLE:    objSize = sizeof(ObjTable);    break;
    case OBJ_NATIVE:   objSize = sizeof(ObjNative);   break;
    }

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
    case OBJ_UPVALUE:
        break;
    case OBJ_TABLE: {
        ObjTable* table = (ObjTable*)object;
        if (table->array != NULL) {
            reallocate(table->array, sizeof(Value) * table->arrayCapacity, 0);
        }
        freeTable(&table->fields);
        break;
    }
    case OBJ_NATIVE:
        break;
    }

    reallocate(object, objSize, 0);
}