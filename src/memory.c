#include <stdlib.h>
#include "memory.h"
#include "vm.h"

static bool inCompilation = false;

void setCompiling(bool compiling) {
    inCompilation = compiling;
}

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize > oldSize) {
        vm.bytesAllocated += newSize - oldSize;
    } else if (newSize < oldSize) {
        vm.bytesAllocated -= oldSize - newSize;
    }

    if (!inCompilation && vm.bytesAllocated > vm.nextGC
        && vm.gcPhase == GC_PHASE_IDLE) {
        vm.gcPhase = GC_PHASE_MARK;
        markRoots();
    }

    // Start a fresh mark whenever the threshold is crossed outside of compilation.
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);

    return result;
}