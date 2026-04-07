#ifndef aul_vm_h
#define aul_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * 250)

#define GC_HEAP_GROW_FACTOR 2

typedef struct {
    ObjClosure* closure;
    uint32_t* ip;
    Value* slots;
} CallFrame;

typedef enum {
    GC_PHASE_MARK_ROOTS,
    GC_PHASE_MARK,
    GC_PHASE_SWEEP,
    GC_PHASE_IDLE
} GCPhase;

typedef struct {
    struct Obj* objects;
    struct Obj* grayStack;
    struct ObjUpvalue* openUpvalues;

    Chunk* chunk;
    
    Value stack[STACK_MAX];
    Value* stackTop;

    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Table globals;
    
    GCPhase gcPhase;
    size_t bytesAllocated;
    size_t nextGC;
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
