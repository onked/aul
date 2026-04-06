#include <stdio.h>
#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk *chunk, const char *name)
{
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;)
    {
        offset = disassembleInstruction(chunk, offset);
    }
}

// Format: OP_NAME  SlotA  SlotB  SlotC
static int abcInstruction(const char *name, uint32_t instruction, int offset)
{
    uint8_t a = GET_A(instruction);
    uint8_t b = GET_B(instruction);
    uint8_t c = GET_C(instruction);
    printf("%-16s %4d %4d %4d\n", name, a, b, c);
    return offset + 1;
}

// Format: OP_NAME  SlotA  Constant[Bx]
static int abxInstruction(const char *name, Chunk *chunk, uint32_t instruction, int offset)
{
    uint8_t a = GET_A(instruction);
    uint16_t bx = GET_Bx(instruction);

    printf("%-16s %4d %4d '", name, a, bx);
    if (bx < chunk->constants.count)
    {
        printValue(chunk->constants.values[bx]);
    }
    else
    {
        printf("INVALID INDEX");
    }
    printf("'\n");

    return offset + 1;
}

// Helper for instructions that only use Register A
static int simpleRegister(const char *name, uint32_t instruction, int offset)
{
    printf("%-16s %4d\n", name, GET_A(instruction));
    return offset + 1;
}

int disassembleInstruction(Chunk *chunk, int offset)
{
    printf("%04d ", offset);

    uint32_t instruction = chunk->code[offset];
    OpCode op = GET_OP(instruction);

    switch (op)
    {
    case OP_CONSTANT:
        return abxInstruction("OP_CONSTANT", chunk, instruction, offset);
    case OP_DEFINE_GLOBAL:
        return abxInstruction("OP_DEFINE_GLOBAL", chunk, instruction, offset);
    case OP_GET_GLOBAL:
        return abxInstruction("OP_GET_GLOBAL", chunk, instruction, offset);
    case OP_SET_GLOBAL:
        return abxInstruction("OP_SET_GLOBAL", chunk, instruction, offset);

    case OP_MOVE:
        // Move typically uses A (dest) and B (source)
        return abcInstruction("OP_MOVE", instruction, offset);

    case OP_PRINT:
        return simpleRegister("OP_PRINT", instruction, offset);

    case OP_TRUE:
        return simpleRegister("OP_TRUE", instruction, offset);
    case OP_FALSE:
        return simpleRegister("OP_FALSE", instruction, offset);
    case OP_NIL:
        return simpleRegister("OP_NIL", instruction, offset);

    case OP_ADD:
        return abcInstruction("OP_ADD", instruction, offset);
    case OP_SUBTRACT:
        return abcInstruction("OP_SUBTRACT", instruction, offset);
    case OP_MULTIPLY:
        return abcInstruction("OP_MULTIPLY", instruction, offset);
    case OP_DIVIDE:
        return abcInstruction("OP_DIVIDE", instruction, offset);
    case OP_NEGATE:
        return abcInstruction("OP_NEGATE", instruction, offset);

    case OP_GET_UPVALUE:
        // A is the destination register, B is the upvalue index
        return abcInstruction("OP_GET_UPVALUE", instruction, offset);
    case OP_SET_UPVALUE:
        // A is the upvalue index, B is the source register
        return abcInstruction("OP_SET_UPVALUE", instruction, offset);

    case OP_RETURN:
        return simpleRegister("OP_RETURN", instruction, offset);

    case OP_JUMP:
        uint16_t jumpOffset = GET_Bx(instruction); 
        printf("%-16s %4d\n", "OP_JUMP", jumpOffset);
        return offset + 1; 

    case OP_CALL:
        // A is the function register, B is the arg count
        return abcInstruction("OP_CALL", instruction, offset);

    default:
        printf("Unknown opcode %d\n", op);
        return offset + 1;
    }
}