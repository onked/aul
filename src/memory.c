#include <stdlib.h>
#include "memory.h"
#include "vm.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize > oldSize) {
        vm.bytesAllocated += newSize - oldSize;
    }
    
    if (vm.bytesAllocated > vm.nextGC) {
        collectGarbage();
    }
    
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    
    return result;
}
