#include <stdio.h>
#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count; ) {
        offset = disassembleInstruction(chunk, offset);
    }
}

// Format: OP_NAME  SlotA  SlotB  SlotC
static int abcInstruction(const char* name, uint32_t instruction, int offset) {
    uint8_t a = GET_A(instruction);
    uint8_t b = GET_B(instruction);
    uint8_t c = GET_C(instruction);
    printf("%-16s %4d %4d %4d\n", name, a, b, c);
    return offset + 1;
}

// Format: OP_NAME  SlotA  Constant[Bx]
static int abxInstruction(const char* name, Chunk* chunk, uint32_t instruction, int offset) {
    uint8_t a = GET_A(instruction);
    uint16_t bx = GET_Bx(instruction);
    
    printf("%-16s %4d %4d '", name, a, bx);
    if (bx < chunk->constants.count) {
        printValue(chunk->constants.values[bx]);
    } else {
        printf("INVALID INDEX");
    }
    printf("'\n");
    
    return offset + 1;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    // Pull the full 32-bit instruction
    uint32_t instruction = chunk->code[offset];
    OpCode op = GET_OP(instruction);

    switch (op) {
        case OP_CONSTANT:
            return abxInstruction("OP_CONSTANT", chunk, instruction, offset);
        case OP_DEFINE_GLOBAL:
            return abxInstruction("OP_DEFINE_GLOBAL", chunk, instruction, offset);
        case OP_GET_GLOBAL:
            return abxInstruction("OP_GET_GLOBAL", chunk, instruction, offset);
        case OP_SET_GLOBAL:
            return abxInstruction("OP_SET_GLOBAL", chunk, instruction, offset);
        case OP_PRINT:
            // Print only uses Register A
            printf("%-16s %4d\n", "OP_PRINT", GET_A(instruction));
            return offset + 1;
        case OP_ADD:
            return abcInstruction("OP_ADD", instruction, offset);
        case OP_SUBTRACT:
            return abcInstruction("OP_SUBTRACT", instruction, offset);
        case OP_MULTIPLY:
            return abcInstruction("OP_MULTIPLY", instruction, offset);
        case OP_DIVIDE:
            return abcInstruction("OP_DIVIDE", instruction, offset);
        case OP_NEGATE:
            // Negate usually uses A (dest) and B (source)
            printf("%-16s %4d %4d\n", "OP_NEGATE", GET_A(instruction), GET_B(instruction));
            return offset + 1;
        case OP_RETURN:
            // Return can optionally return the value in Register A
            printf("%-16s %4d\n", "OP_RETURN", GET_A(instruction));
            return offset + 1;
        default:
            printf("Unknown opcode %d\n", op);
            return offset + 1;
    }
}