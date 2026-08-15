#include "chunk.h"
#include "value.h"
#include "memory.h"

void specializeTypes(Chunk* chunk) {
    if (chunk->count == 0) return;

    int n = chunk->count;
    int nconst = chunk->constants.count;

    uint8_t* fwdTargets = (uint8_t*)reallocate(NULL, 0, n);
    memset(fwdTargets, 0, n);
    for (int i = 0; i < n; i++) {
        uint32_t inst = chunk->code[i];
        OpCode op = GET_OP(inst);
        int target = -1;
        switch (op) {
            case OP_JUMP:
                target = i + 1 + (int16_t)GET_Bx(inst);
                break;
            case OP_JUMP_IF_FALSE:
                target = i + 1 + GET_Bx(inst);
                break;
            case OP_INT_JLT: case OP_INT_JLE: case OP_INT_JGT:
            case OP_INT_JGE: case OP_INT_JE:
                target = i + 1 + (int8_t)GET_C(inst);
                break;
            default:
                break;
        }
        if (target > i && target < n) {
            fwdTargets[target] = 1;
        }
    }

    uint8_t isInt[256] = {0};
    uint8_t* globalInt = (uint8_t*)reallocate(NULL, 0, nconst > 0 ? nconst : 1);
    memset(globalInt, 0, nconst > 0 ? nconst : 1);
    uint8_t upvalInt[256] = {0};

    for (int i = 0; i < n; i++) {
        uint32_t inst = chunk->code[i];
        OpCode op = GET_OP(inst);
        uint8_t a = GET_A(inst);
        uint8_t b = GET_B(inst);
        uint8_t c = GET_C(inst);
        uint16_t bx = GET_Bx(inst);

        if (fwdTargets[i]) {
            memset(isInt, 0, sizeof(isInt));
            memset(globalInt, 0, nconst > 0 ? nconst : 1);
            memset(upvalInt, 0, sizeof(upvalInt));
        }

        switch (op) {
            case OP_CONSTANT:
                isInt[a] = IS_INTEGER(chunk->constants.values[bx]) ? 1 : 0;
                break;

            case OP_MOVE:
                isInt[a] = isInt[b];
                break;

            case OP_NIL: case OP_TRUE: case OP_FALSE:
            case OP_TABLE: case OP_NOT: case OP_SQRT:
                isInt[a] = 0;
                break;

            case OP_INCREMENT: {
                if (isInt[a]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_INCREMENT, a, 0, 0);
                }
                break;
            }

            case OP_NEGATE: {
                if (isInt[b]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_NEGATE, a, b, 0);
                    isInt[a] = 1;
                } else {
                    isInt[a] = 0;
                }
                break;
            }

            case OP_ADD: {
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_ADD, a, b, c);
                    isInt[a] = 1;
                } else {
                    isInt[a] = 0;
                }
                break;
            }

            case OP_ADD_BUF: {
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_ADD, a, b, c);
                    isInt[a] = 1;
                } else {
                    isInt[a] = 0;
                }
                break;
            }

            case OP_SUBTRACT:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_SUBTRACT, a, b, c);
                    isInt[a] = 1;
                } else {
                    isInt[a] = 0;
                }
                break;

            case OP_MULTIPLY:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_MULTIPLY, a, b, c);
                    isInt[a] = 1;
                } else {
                    isInt[a] = 0;
                }
                break;

            case OP_MODULO:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_MODULO, a, b, c);
                    isInt[a] = 1;
                } else {
                    isInt[a] = 0;
                }
                break;

            case OP_DIVIDE:
                isInt[a] = 0;
                break;

            case OP_LESS:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_LESS, a, b, c);
                }
                isInt[a] = 0;
                break;

            case OP_GREATER:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_GREATER, a, b, c);
                }
                isInt[a] = 0;
                break;

            case OP_LESS_EQUAL:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_LESS_EQUAL, a, b, c);
                }
                isInt[a] = 0;
                break;

            case OP_GREATER_EQUAL:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_GREATER_EQUAL, a, b, c);
                }
                isInt[a] = 0;
                break;

            case OP_EQUAL:
                if (isInt[b] && isInt[c]) {
                    chunk->code[i] = CREATE_ABC(OP_INT_EQUAL, a, b, c);
                }
                isInt[a] = 0;
                break;

            case OP_NOT_EQUAL:
                isInt[a] = 0;
                break;

            case OP_GET_GLOBAL:
                isInt[a] = globalInt[bx];
                break;

            case OP_SET_GLOBAL:
                globalInt[bx] = isInt[a];
                break;

            case OP_DEFINE_GLOBAL:
                globalInt[bx] = isInt[a];
                break;

            case OP_GET_UPVALUE:
                isInt[a] = upvalInt[b];
                break;

            case OP_SET_UPVALUE:
                upvalInt[a] = isInt[b];
                break;

            case OP_GET_READONLY_UPVALUE:
                isInt[a] = 0;
                break;

            case OP_FOR_IN:
                isInt[a + 1] = 0;
                isInt[a + 2] = 0;
                break;

            case OP_TRY:
                isInt[a] = 0;
                break;

            case OP_CALL: case OP_GET_TABLE: case OP_GET_METATABLE:
            case OP_LENGTH: case OP_CLOCK: case OP_GET_RET:
                isInt[a] = 0;
                break;

            case OP_JUMP: case OP_JUMP_IF_FALSE:
            case OP_INT_JLT: case OP_INT_JLE: case OP_INT_JGT:
            case OP_INT_JGE: case OP_INT_JE:
            case OP_SET_TABLE: case OP_SET_METATABLE:
            case OP_PRINT: case OP_POP: case OP_CLOSURE:
            case OP_RETURN: case OP_RETURN_MULTI: case OP_NOP: case OP_CONTINUE:
            case OP_BREAK: case OP_ENDTRY:
                break;

            default:
                break;
        }

        bool backward = false;
        switch (op) {
            case OP_JUMP:
                backward = (i + 1 + (int16_t)bx < i);
                break;
            case OP_INT_JLT: case OP_INT_JLE: case OP_INT_JGT:
            case OP_INT_JGE: case OP_INT_JE:
                backward = (i + 1 + (int8_t)c < i);
                break;
            default:
                break;
        }
        if (backward) {
            memset(isInt, 0, sizeof(isInt));
            memset(globalInt, 0, nconst > 0 ? nconst : 1);
            memset(upvalInt, 0, sizeof(upvalInt));
        }
    }

    reallocate(fwdTargets, n, 0);
    reallocate(globalInt, nconst > 0 ? nconst : 1, 0);
}
