#include <stdio.h>
#include "debug.h"
#include "value.h"
#include "object.h"

void disassembleChunk(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int abcInstruction(const char *name, uint32_t instruction, int offset) {
    uint8_t a = GET_A(instruction);
    uint8_t b = GET_B(instruction);
    uint8_t c = GET_C(instruction);
    printf("%-16s %4d %4d %4d\n", name, a, b, c);
    return offset + 1;
}

static int abxInstruction(const char *name, Chunk *chunk, uint32_t instruction, int offset) {
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

static int simpleRegister(const char *name, uint32_t instruction, int offset) {
    printf("%-16s %4d\n", name, GET_A(instruction));
    return offset + 1;
}

int disassembleInstruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);
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
        case OP_MOVE:
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
            return abcInstruction("OP_GET_UPVALUE", instruction, offset);
        case OP_SET_UPVALUE:
            return abcInstruction("OP_SET_UPVALUE", instruction, offset);
        case OP_RETURN:
            return simpleRegister("OP_RETURN", instruction, offset);
        case OP_JUMP: {
            uint16_t jumpOffset = GET_Bx(instruction);
            printf("%-16s %4d\n", "OP_JUMP", jumpOffset);
            return offset + 1;
        }
        case OP_JUMP_IF_FALSE: {
            uint8_t a = GET_A(instruction);
            uint16_t jOffset = GET_Bx(instruction);
            printf("%-16s %4d %4d\n", "OP_JUMP_IF_FALSE", a, jOffset);
            return offset + 1;
        }
        case OP_POP:
            return simpleRegister("OP_POP", instruction, offset);
        case OP_EQUAL:
            return abcInstruction("OP_EQUAL", instruction, offset);
        case OP_GREATER:
            return abcInstruction("OP_GREATER", instruction, offset);
        case OP_LESS:
            return abcInstruction("OP_LESS", instruction, offset);
        case OP_GREATER_EQUAL:
            return abcInstruction("OP_GREATER_EQUAL", instruction, offset);
        case OP_LESS_EQUAL:
            return abcInstruction("OP_LESS_EQUAL", instruction, offset);
        case OP_NOT:
            return abcInstruction("OP_NOT", instruction, offset);
        case OP_LENGTH:
            return abcInstruction("OP_LENGTH", instruction, offset);
        case OP_SET_METATABLE:
            return abcInstruction("OP_SET_METATABLE", instruction, offset);
        case OP_GET_METATABLE:
            return abcInstruction("OP_GET_METATABLE", instruction, offset);
        case OP_TABLE:
            return simpleRegister("OP_TABLE", instruction, offset);
        case OP_GET_TABLE:
            return abcInstruction("OP_GET_TABLE", instruction, offset);
        case OP_SET_TABLE:
            return abcInstruction("OP_SET_TABLE", instruction, offset);
        case OP_CALL:
            return abcInstruction("OP_CALL", instruction, offset);
        case OP_CLOSURE: {
            uint8_t a = GET_A(instruction);
            uint16_t bx = GET_Bx(instruction);
            printf("%-16s %4d %4d '", "OP_CLOSURE", a, bx);
            if (bx < chunk->constants.count) {
                printValue(chunk->constants.values[bx]);
            } else {
                printf("INVALID INDEX");
            }
            printf("'\n");
            ObjFunction* func = AS_FUNCTION(chunk->constants.values[bx]);
            for (int i = 0; i < func->upvalueCount; i++) {
                uint8_t isLocal = chunk->code[offset + 1 + i * 2];
                uint8_t index = chunk->code[offset + 1 + i * 2 + 1];
                printf("        %04d |                      %s %d\n",
                       offset + 1 + i * 2,
                       isLocal ? "local" : "upvalue",
                       index);
            }
            return offset + 1 + func->upvalueCount * 2;
        }
        default:
            printf("Unknown opcode %d\n", op);
            return offset + 1;
    }
}
