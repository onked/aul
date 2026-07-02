#ifndef aul_vm_h
#define aul_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * 250)

#define GC_HEAP_GROW_FACTOR 2

#define IC_SIZE 64

typedef struct {
    bool valid;
    Chunk* chunk;
    int instOffset;
    Value table;
    uint32_t tableGen;
    Value key;
    Value result;
} InlineCache;

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
    // Array-based gray stack so we don't corrupt the vm.objects linked list
    struct Obj** grayStack;
    int grayCount;
    int grayCapacity;
    struct ObjUpvalue* openUpvalues;
    
    Chunk* chunk;
    
    Value stack[STACK_MAX];
    
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    
    Table globals;
    Table strings;
    
    GCPhase gcPhase;
    size_t bytesAllocated;
    size_t nextGC;
    
    Obj* sweepObj;
    size_t marksPerStep;

    struct ObjString* mmIndex;
    struct ObjString* mmNewIndex;
    struct ObjString* mmCall;
    struct ObjString* mmLen;

    InlineCache inlineCache[IC_SIZE];
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
void gcStep(void);

#endif
