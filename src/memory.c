#include <stdlib.h>
#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    // Silence unused parameter warning to prevent build failure
    (void)oldSize; 

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    
    // Safety exit if the OS fails to allocate
    if (result == NULL) exit(1);
    
    return result;
}