#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint32_t instruction, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = (oldCapacity < 8) ? 8 : oldCapacity * 2;
        chunk->code = reallocate(chunk->code, oldCapacity * sizeof(uint32_t),
                                 chunk->capacity * sizeof(uint32_t));
        chunk->lines = reallocate(chunk->lines, oldCapacity * sizeof(int),
                                  chunk->capacity * sizeof(int));
    }
    chunk->code[chunk->count] = instruction;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

void freeChunk(Chunk* chunk) {
    reallocate(chunk->code, sizeof(uint32_t) * chunk->capacity, 0);
    reallocate(chunk->lines, sizeof(int) * chunk->capacity, 0);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

int addConstant(Chunk* chunk, Value value) {
    writeValueArray(&chunk->constants, value);
    return chunk->constants.count - 1;
}
