#ifndef aul_vm_h
#define aul_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * 250) // Enough space for all frames

typedef struct {
    uint32_t* ip;           // The "Return Address" for this specific function
    Value* slots;           // Pointer to the first register this frame can use in the VM stack
} CallFrame;

typedef struct {
    Chunk* chunk;
    
    // The "Physical" memory for all registers across all functions
    Value stack[STACK_MAX]; 
    Value* stackTop; 

    // The Call Stack
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Table globals;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm; 

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

#endif