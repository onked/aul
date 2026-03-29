#ifndef aul_chunk_h
#define aul_chunk_h

#include <stdint.h>
#include "value.h"

typedef enum {
    OP_CONSTANT,      // Load a value from the constant pool into a specific register slot.
    OP_DEFINE_GLOBAL, // Create a new global variable using a name from constants and a value from a register.
    OP_GET_GLOBAL,    // Look up a global variable by name and copy its value into a register slot.
    OP_SET_GLOBAL,    // Update an existing global variable with a value from a register slot.
    OP_PRINT,         // Print the value currently stored in a specific register slot.
    OP_ADD,           // Add values from two registers and store the result in a third register.
    OP_SUBTRACT,      // Subtract one register from another and store the result in a third register.
    OP_MULTIPLY,      // Multiply two registers and store the result in a third register.
    OP_DIVIDE,        // Divide one register by another and store the result in a third register.
    OP_NEGATE,        // Flip the sign of a value in one register and store it in another.
    OP_MOVE,    // Copy value from Register B to Register A
    OP_NIL,     // Specifically load NIL into a register (faster than OP_CONSTANT)
    OP_TRUE,    // Load TRUE into a register
    OP_FALSE,   // Load FALSE into a register
    OP_JUMP,          // Unconditional jump forward
    OP_CALL,          // Call a function at a specific register with a certain number of arguments
    OP_RETURN,        // Exit the current function or script.
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint32_t* code;
    int* lines;
    ValueArray constants;
} Chunk;

// Macros to pack the slot numbers into the 32-bit instruction
#define CREATE_ABC(op, a, b, c) ((uint32_t)(((op) & 0xFF) | ((a) << 8) | ((b) << 16) | ((c) << 24)))
#define CREATE_ABx(op, a, bx)   ((uint32_t)(((op) & 0xFF) | ((a) << 8) | ((bx) << 16)))

// Macros for the VM to unpack and read those slot numbers
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