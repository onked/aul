#ifndef aul_memory_h
#define aul_memory_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "object.h"

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void setCompiling(bool compiling);

void markObject(Obj* object);
void markValue(Value value);
void markArray(ValueArray* array);
void blackenObject(Obj* object);
void markRoots(void);
void traceReferences(void);
void sweep(void);
void collectGarbage();
void freeObject(Obj* object);

void gcWriteBarrier(Value value);

#endif
