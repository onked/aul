#ifndef aul_vm_h
#define aul_vm_h

#include "chunk.h"
#include "value.h" // We'll move the Value typedef here shortly

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;          // The code we are currently running
  uint8_t* ip;           // "Instruction Pointer" - points to the next byte to execute
  
  Value stack[STACK_MAX]; // The VM's workspace
  Value* stackTop;        // Points to where the next value goes
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

// Stack helpers
void push(Value value);
Value pop();

#endif