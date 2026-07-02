#ifndef aul_chunk_h
#define aul_chunk_h

#include <stdint.h>
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_READONLY_UPVALUE,
    OP_PRINT,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_GREATER_EQUAL,
    OP_LESS_EQUAL,
    OP_NOT,
    OP_BREAK,
    OP_CONTINUE,
    OP_TABLE,
    OP_GET_TABLE,
    OP_SET_TABLE,
    OP_LENGTH,
    OP_SET_METATABLE,
    OP_GET_METATABLE,
    OP_MOVE,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_POP,
    OP_CALL,
    OP_RETURN,
    OP_CLOSURE,
    OP_CLOCK,
    OP_INCREMENT,
    OP_NOP,
    OP_INT_ADD,
    OP_INT_SUBTRACT,
    OP_INT_MULTIPLY,
    OP_INT_LESS,
    OP_INT_GREATER,
    OP_INT_LESS_EQUAL,
    OP_INT_GREATER_EQUAL,
    OP_INT_EQUAL,
    OP_INT_NEGATE,
    OP_INT_INCREMENT,
    OP_INT_JLT,
    OP_INT_JLE,
    OP_INT_JGT,
    OP_INT_JGE,
    OP_INT_JE,
    OP_NOT_EQUAL,
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint32_t* code;
    int* lines;
    ValueArray constants;
} Chunk;

#define CREATE_ABC(op, a, b, c) ((uint32_t)(((op) & 0xFF) | ((a) << 8) | ((b) << 16) | ((c) << 24)))
#define CREATE_ABx(op, a, bx)   ((uint32_t)(((op) & 0xFF) | ((a) << 8) | ((bx) << 16)))

#define GET_OP(inst) ((OpCode)((inst) & 0xFF))
#define GET_A(inst)  ((uint8_t)(((inst) >> 8) & 0xFF))
#define GET_B(inst)  ((uint8_t)(((inst) >> 16) & 0xFF))
#define GET_C(inst)  ((uint8_t)(((inst) >> 24) & 0xFF))
#define GET_Bx(inst) ((uint16_t)(((inst) >> 16) & 0xFFFF))

void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint32_t instruction, int line);
void freeChunk(Chunk* chunk);
int addConstant(Chunk* chunk, Value value);

#endif
