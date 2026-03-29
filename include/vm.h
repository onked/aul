#ifndef aul_vm_h
#define aul_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  uint32_t* ip;
  
  Value registers[STACK_MAX]; 
  Value* stackTop; 

  Table globals;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

// Making the VM instance globally accessible across the project
extern VM vm; 

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

// Stack helpers
void push(Value value);
Value pop();

#endif