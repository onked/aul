#include "chunk.h"
#include "value.h"

// Type inference
void specializeTypes(Chunk* chunk) {
    if (chunk->count == 0) return;

    int isInt[256] = {0};

    for (int i = 0; i < chunk->count; i++) {
        uint32_t inst = chunk->code[i];
        OpCode op = GET_OP(inst);
        uint8_t a = GET_A(inst);
        uint8_t b = GET_B(inst);
        uint8_t c = GET_C(inst);
        uint16_t bx = GET_Bx(inst);

        switch (op) {
            case OP_CONSTANT:
                isInt[a] = IS_INTEGER(chunk->constants.values[bx]) ? 1 : 0;
                break;

            case OP_MOVE:
                isInt[a] = isInt[b];
                break;

            case OP_NIL: case OP_TRUE: case OP_FALSE:
            case OP_TABLE:
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

            case OP_DIVIDE: case OP_MODULO:
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

            case OP_JUMP_IF_FALSE:
                break;

            case OP_CALL: case OP_GET_UPVALUE:
            case OP_GET_READONLY_UPVALUE: case OP_GET_GLOBAL:
            case OP_GET_TABLE: case OP_GET_METATABLE:
            case OP_LENGTH: case OP_CLOCK:
                isInt[a] = 0;
                break;

            case OP_SET_UPVALUE: case OP_SET_GLOBAL: case OP_SET_TABLE:
            case OP_SET_METATABLE:
            case OP_PRINT: case OP_JUMP: case OP_POP:
            case OP_DEFINE_GLOBAL: case OP_CLOSURE:
            case OP_RETURN: case OP_NOP: case OP_CONTINUE:
            case OP_BREAK:
                break;

            default:
                break;
        }
    }
}
