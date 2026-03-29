#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

// Initialize a chunk to a clean, empty state.
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL; 
    initValueArray(&chunk->constants);
}

// Add a single 32-bit instruction to the stream.
void writeChunk(Chunk* chunk, uint32_t instruction, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = (oldCapacity < 8) ? 8 : oldCapacity * 2;
        
        // We now allocate 4 bytes (sizeof(uint32_t)) for every instruction.
        chunk->code = reallocate(chunk->code, oldCapacity * sizeof(uint32_t), 
                                 chunk->capacity * sizeof(uint32_t));
        chunk->lines = reallocate(chunk->lines, oldCapacity * sizeof(int), 
                                  chunk->capacity * sizeof(int));
    }

    chunk->code[chunk->count] = instruction;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

// Wipes the chunk from memory.
void freeChunk(Chunk* chunk) {
    reallocate(chunk->code, sizeof(uint32_t) * chunk->capacity, 0);
    reallocate(chunk->lines, sizeof(int) * chunk->capacity, 0);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

// Returns the index of the added constant.
int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1; 
}