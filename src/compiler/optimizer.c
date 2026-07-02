#include "chunk.h"
#include "value.h"
#include <string.h>

static void adjustJumpsCrossing(Chunk* chunk, int removePos) {
    for (int j = 0; j < chunk->count; j++) {
        if (j == removePos) continue;
        uint32_t inst = chunk->code[j];
        OpCode op = GET_OP(inst);
        int16_t off;
        switch (op) {
            case OP_JUMP: case OP_JUMP_IF_FALSE:
                off = (int16_t)GET_Bx(inst); break;
            case OP_INT_JLT: case OP_INT_JLE:
            case OP_INT_JGT: case OP_INT_JGE:
            case OP_INT_JE:
                off = (int8_t)GET_C(inst); break;
            default: continue;
        }
        int16_t target = (int16_t)(j + 1) + off;
        if (j < removePos && target > removePos) {
            if (op == OP_JUMP || op == OP_JUMP_IF_FALSE)
                chunk->code[j] = CREATE_ABx(op, GET_A(inst), (uint16_t)(off - 1));
            else
                chunk->code[j] = CREATE_ABC(op, GET_A(inst), GET_B(inst), (uint8_t)((off - 1) & 0xFF));
        } else if (j > removePos && target <= removePos - 1) {
            if (op == OP_JUMP || op == OP_JUMP_IF_FALSE)
                chunk->code[j] = CREATE_ABx(op, GET_A(inst), (uint16_t)(off + 1));
            else
                chunk->code[j] = CREATE_ABC(op, GET_A(inst), GET_B(inst), (uint8_t)((off + 1) & 0xFF));
        }
    }
}

void removeNops(Chunk* chunk) {
    int i = 0;
    while (i < chunk->count) {
        if (GET_OP(chunk->code[i]) == OP_NOP) {
            adjustJumpsCrossing(chunk, i);
            memmove(&chunk->code[i], &chunk->code[i + 1],
                    (chunk->count - i - 1) * sizeof(uint32_t));
            chunk->count--;
        } else {
            i++;
        }
    }
}

static int instDestReg(uint32_t inst) {
    switch (GET_OP(inst)) {
        case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_PRINT:
        case OP_DEFINE_GLOBAL: case OP_SET_GLOBAL:
        case OP_SET_UPVALUE: case OP_SET_TABLE: case OP_RETURN:
        case OP_INT_JLT: case OP_INT_JLE: case OP_INT_JGT:
        case OP_INT_JGE: case OP_INT_JE:
            return -1;
        default:
            return GET_A(inst);
    }
}

static bool instReadsReg(uint32_t inst, uint8_t reg) {
    switch (GET_OP(inst)) {
        case OP_ADD: case OP_SUBTRACT: case OP_MULTIPLY:
        case OP_DIVIDE: case OP_MODULO:
        case OP_EQUAL: case OP_NOT_EQUAL: case OP_GREATER: case OP_LESS:
        case OP_GREATER_EQUAL: case OP_LESS_EQUAL:
        case OP_GET_TABLE: case OP_SET_TABLE:
        case OP_INT_ADD: case OP_INT_SUBTRACT: case OP_INT_MULTIPLY:
        case OP_INT_LESS: case OP_INT_GREATER:
        case OP_INT_LESS_EQUAL: case OP_INT_GREATER_EQUAL:
        case OP_INT_EQUAL:
            return GET_B(inst) == reg || GET_C(inst) == reg;
        case OP_INT_JLT: case OP_INT_JLE: case OP_INT_JGT:
        case OP_INT_JGE: case OP_INT_JE:
            return GET_A(inst) == reg || GET_B(inst) == reg;
        case OP_MOVE: case OP_NEGATE: case OP_NOT: case OP_LENGTH:
        case OP_JUMP_IF_FALSE: case OP_GET_METATABLE:
        case OP_GET_UPVALUE: case OP_GET_READONLY_UPVALUE:
        case OP_DEFINE_GLOBAL: case OP_SET_GLOBAL: case OP_SET_UPVALUE:
        case OP_INT_NEGATE:
            return GET_B(inst) == reg;
        case OP_CALL:
        case OP_INCREMENT:
        case OP_INT_INCREMENT:
            return GET_A(inst) == reg;
        case OP_CONSTANT: case OP_CLOSURE: case OP_GET_GLOBAL:
        case OP_JUMP: case OP_NIL: case OP_TRUE: case OP_FALSE:
        case OP_TABLE: case OP_NOP: case OP_CLOCK:
        case OP_RETURN: case OP_PRINT: case OP_POP:
        case OP_SET_METATABLE:
            return false;
        default:
            return false;
    }
}

void foldCompareJumps(Chunk* chunk) {
    if (chunk->count < 2) return;
    for (int i = 0; i < chunk->count - 1; i++) {
        uint32_t inst1 = chunk->code[i];
        uint32_t inst2 = chunk->code[i + 1];
        if (GET_OP(inst2) != OP_JUMP_IF_FALSE) continue;
        if (GET_A(inst1) != GET_A(inst2)) continue;

        OpCode cmpOp = GET_OP(inst1);
        OpCode combinedOp;
        switch (cmpOp) {
            case OP_INT_LESS:           combinedOp = OP_INT_JLT; break;
            case OP_INT_LESS_EQUAL:     combinedOp = OP_INT_JLE; break;
            case OP_INT_GREATER:        combinedOp = OP_INT_JGT; break;
            case OP_INT_GREATER_EQUAL:  combinedOp = OP_INT_JGE; break;
            case OP_INT_EQUAL:          combinedOp = OP_INT_JE; break;
            default: continue;
        }

        int16_t oldOffset = (int16_t)GET_Bx(inst2);
        int16_t newOffset = oldOffset + 1;
        if (newOffset < -128 || newOffset > 127) continue;

        chunk->code[i] = CREATE_ABC(combinedOp, GET_B(inst1), GET_C(inst1), (uint8_t)(newOffset & 0xFF));

        adjustJumpsCrossing(chunk, i + 1);
        memmove(&chunk->code[i + 1], &chunk->code[i + 2],
                (chunk->count - i - 2) * sizeof(uint32_t));
        chunk->count--;
    }
}

void optimizeChunk(Chunk* chunk) {
    if (chunk->count < 2) return;

    for (int i = 0; i < chunk->count - 1; i++) {
        uint32_t inst1 = chunk->code[i];
        uint32_t inst2 = chunk->code[i + 1];

        if (GET_OP(inst1) == OP_ADD && GET_OP(inst2) == OP_MOVE)
        {
            uint8_t addDst = GET_A(inst1);
            uint8_t addLeft = GET_B(inst1);
            uint8_t addRight = GET_C(inst1);
            uint8_t moveDst = GET_A(inst2);
            uint8_t moveSrc = GET_B(inst2);

            if (addDst == moveSrc && addLeft == moveDst) {
                chunk->code[i] = CREATE_ABC(OP_ADD, addLeft, addLeft, addRight);
                chunk->code[i + 1] = CREATE_ABC(OP_NOP, 0, 0, 0);
                continue;
            }
        }

        if (GET_OP(inst1) == OP_MOVE) {
            uint8_t moveDest = GET_A(inst1);
            int dest2 = instDestReg(inst2);
            if (dest2 == moveDest && !instReadsReg(inst2, moveDest)) {
                chunk->code[i] = CREATE_ABC(OP_NOP, 0, 0, 0);
                continue;
            }
        }

        if (i < chunk->count - 2) {
            uint32_t inst3 = chunk->code[i + 2];

            if (GET_OP(inst1) != OP_CONSTANT) continue;
            if (GET_OP(inst2) != OP_ADD) continue;
            if (GET_OP(inst3) != OP_MOVE) continue;

            uint8_t constReg = GET_A(inst1);
            uint16_t constIdx = GET_Bx(inst1);
            uint8_t addDest = GET_A(inst2);
            uint8_t addLeft = GET_B(inst2);
            uint8_t addRight = GET_C(inst2);
            uint8_t moveDest = GET_A(inst3);
            uint8_t moveSrc = GET_B(inst3);

            if (addRight != constReg) continue;
            if (addDest != moveSrc) continue;
            if (addLeft != moveDest) continue;

            Value constVal = chunk->constants.values[constIdx];
            if (!IS_INTEGER(constVal)) continue;
            if (AS_INTEGER(constVal) != 1) continue;

            chunk->code[i] = CREATE_ABC(OP_INCREMENT, addLeft, 0, 0);
            chunk->code[i + 1] = CREATE_ABC(OP_NOP, 0, 0, 0);
            chunk->code[i + 2] = CREATE_ABC(OP_NOP, 0, 0, 0);
        }
    }
}
