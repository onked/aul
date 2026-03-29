#include <stdio.h>
#include "debug.h"

// Disassembles the whole chunk
void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count; ) {
        offset = disassembleInstruction(chunk, offset);
    }
}

// A helper for simple 1-byte instructions
static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

// A helper for instructions that take an argument (like OP_CONSTANT)
static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    // Check if we are at the end of the chunk to avoid reading out of bounds
    if (offset + 1 >= chunk->count) {
        printf("%-16s %4s 'ERROR: Missing Index'\n", name, "");
        return offset + 1;
    }

    uint8_t constantIndex = chunk->code[offset + 1];
    
    printf("%-16s %4d '", name, constantIndex);
    
    // Safety check: ensure the index exists in the constant pool
    if (constantIndex < chunk->constants.count) {
        printf("%g", chunk->constants.values[constantIndex]);
    } else {
        printf("INVALID INDEX");
    }
    
    printf("'\n");
    
    return offset + 2;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_ADD:       return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:  return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:  return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:    return simpleInstruction("OP_DIVIDE", offset);
        case OP_NEGATE:    return simpleInstruction("OP_NEGATE", offset);
        case OP_RETURN:    return simpleInstruction("OP_RETURN", offset);
        case OP_POP:       return simpleInstruction("OP_POP", offset);
        case OP_DEFINE_GLOBAL:  return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_RETURN_VALUE:   return simpleInstruction("OP_RETURN_VALUE", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}