#ifndef aul_chunk_h
#define aul_chunk_h

#include <stdint.h>
#include "value.h"

// OpCodes represent the "instructions" the VM understands.
// Each one is a single byte (uint8_t).
typedef enum {
    OP_CONSTANT,      // Load a constant from the pool
    OP_DEFINE_GLOBAL, // Define a global variable
    OP_POP,
    OP_ADD,           // Add top two stack values
    OP_SUBTRACT,      // Subtract top two stack values
    OP_MULTIPLY,      // Multiply top two stack values
    OP_DIVIDE,        // Divide top two stack values
    OP_NEGATE,        // Flip the sign of the top stack value
    OP_RETURN,        // Exit the current function/script
} OpCode;

// A 'Chunk' is a dynamic array of bytecode instructions 
// plus a corresponding array of constants (the 'ValueArray').
typedef struct {
    int count;           // How many bytes are currently used
    int capacity;        // How many bytes are currently allocated
    uint8_t* code;       // The actual array of bytecode
    ValueArray constants; // The pool of constants (numbers, strings, etc.)
} Chunk;

// Lifecycle and management functions for Chunks
void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte);
void freeChunk(Chunk* chunk);

// Convenience function to add a constant and return its index
int addConstant(Chunk* chunk, Value value);

#endif