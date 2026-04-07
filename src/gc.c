#include <stdlib.h>
#include "memory.h"
#include "vm.h"
#include "object.h"
#include "table.h"

// #define DEBUG_LOG_GC  // uncomment to see gc activity
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#endif

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->marked) return;
    
#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
#endif
    
    object->marked = true;
    object->next = vm.grayStack;
    vm.grayStack = object;
}

void markValue(Value value) {
    if (!IS_OBJ(value)) return;
    markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj* object) {
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
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_UPVALUE:
            break;
        case OBJ_TABLE: {
            ObjTable* table = (ObjTable*)object;
            if (table->metatable != NULL) {
                markObject((Obj*)table->metatable);
            }
            for (int i = 0; i < table->arrayCapacity; i++) {
                markValue(table->array[i]);
            }
            for (int i = 0; i < table->fields.capacity; i++) {
                Entry* entry = &table->fields.entries[i];
                if (entry->key != NULL) {
                    markValue(entry->value);
                }
            }
            break;
        }
    }
}

static void markRoots() {
    for (int i = 0; i < vm.frameCount; i++) {
        CallFrame* frame = &vm.frames[i];
        for (Value* slot = frame->slots; slot < vm.stackTop; slot++) {
            markValue(*slot);
        }
    }
    
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }
    
    for (int i = 0; i < vm.globals.capacity; i++) {
        Entry* entry = &vm.globals.entries[i];
        if (entry->key != NULL) {
            markObject((Obj*)entry->key);
            markValue(entry->value);
        }
    }
    
    if (vm.frameCount > 0) {
        markObject((Obj*)vm.frames[0].closure);
    }
}

static void traceReferences() {
    while (vm.grayStack != NULL) {
        Obj* object = vm.grayStack;
        vm.grayStack = object->next;
        blackenObject(object);
    }
}

static void sweep() {
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
    size_t before = vm.bytesAllocated;
#endif
    
    markRoots();
    traceReferences();
    sweep();
    
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    
#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}

void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif
    
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            free(string->chars);
            free(object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            free(object);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            free(closure->upvalues);
            free(object);
            break;
        }
        case OBJ_UPVALUE: {
            free(object);
            break;
        }
        case OBJ_TABLE: {
            ObjTable* table = (ObjTable*)object;
            free(table->array);
            freeTable(&table->fields);
            free(object);
            break;
        }
    }
}
