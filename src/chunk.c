#include <stdint.h>
#include <stdlib.h>
#include <stddef.h> 

#include "chunk.h"
#include "memory.h"

// Initialize a chunk to a clean, empty state.
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    initValueArray(&chunk->constants);
}

// Add a single byte to the instruction stream.
void writeChunk(Chunk* chunk, uint8_t byte) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        
        // Manual expansion for GROW_ARRAY
        chunk->code = (uint8_t*)reallocate(chunk->code, 
                                           sizeof(uint8_t) * oldCapacity, 
                                           sizeof(uint8_t) * chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->count++;
}

// Wipes the chunk from memory.
void freeChunk(Chunk* chunk) {
    // Manual expansion for FREE_ARRAY
    reallocate(chunk->code, sizeof(uint8_t) * chunk->capacity, 0);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

// Value array
void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        
        array->values = (Value*)reallocate(array->values, 
                                           sizeof(Value) * oldCapacity, 
                                           sizeof(Value) * array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    // Manual expansion for FREE_ARRAY
    reallocate(array->values, sizeof(Value) * array->capacity, 0);
    initValueArray(array);
}

// Returns the index of the added constant.
int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1; 
}