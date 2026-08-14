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
#define GLOBAL_CACHE_SIZE 256

typedef struct {
    ObjString* name;
    uint32_t generation;
    Entry* entry;
} GlobalCacheEntry;

typedef struct {
    ObjClosure* closure;
    uint32_t* ip;
    Value* slots;
} CallFrame;

typedef enum {
    GC_PHASE_MARK,
    GC_PHASE_ATOMIC,
    GC_PHASE_SWEEP,
    GC_PHASE_IDLE
} GCPhase;

typedef struct {
    struct Obj* objects;
    struct Obj* newObjects; // objects born during GC_PHASE_SWEEP; spliced in on completion
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
    uint8_t currentWhite;
    uint8_t otherWhite;
    struct ObjTable* grayagain;
    size_t bytesAllocated;
    size_t nextGC;
    
    Obj* sweepObj;
    Obj* sweepPrev;
    size_t marksPerStep;

    struct ObjString* mmIndex;
    struct ObjString* mmNewIndex;
    struct ObjString* mmCall;
    struct ObjString* mmLen;
    struct ObjString* mmAdd;
    struct ObjString* mmSub;
    struct ObjString* mmMul;
    struct ObjString* mmDiv;

    // Last string concatenation result and the register holding it.
    // OP_ADD_BUF may append in place only when both match (exclusive temp flow).
    Value openString;
    int openStringReg;

    GlobalCacheEntry globalCache[GLOBAL_CACHE_SIZE];
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
InterpretResult run(int baseFrame);
void gcStep(void);
void barrierBack(struct Obj* obj);

#endif
