#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "table.h"
#include "object.h"
#include "memory.h"
#include "libs/math.h"

// uncomment this to see bytecode during execution
// #define DEBUG_TRACE_EXECUTION

VM vm;

static void resetStack()
{
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

// prints errors when the script does something dumb
static void runtimeError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    CallFrame *frame = &vm.frames[vm.frameCount - 1];
    size_t instruction = frame->ip - frame->closure->function->chunk.code - 1;
    int line = frame->closure->function->chunk.lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void initVM()
{
    resetStack();
    vm.objects = NULL;
    vm.grayStack = NULL;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.sweepObj = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 128;
    vm.marksPerStep = 10;
    vm.gcPhase = GC_PHASE_IDLE;
    initTable(&vm.globals);
    initTable(&vm.strings);

    vm.mmIndex    = copyString("__index", 7);
    vm.mmNewIndex = copyString("__newindex", 10);
    vm.mmCall     = copyString("__call", 6);
    vm.mmLen      = copyString("__len", 5);
    vm.mmAdd      = copyString("__add", 5);
    vm.mmSub      = copyString("__sub", 5);
    vm.mmMul      = copyString("__mul", 5);
    vm.mmDiv      = copyString("__div", 5);
    for (int i = 0; i < GLOBAL_CACHE_SIZE; i++) {
        vm.globalCache[i].name = NULL;
    }

    initNativeLibraries();
}

void freeVM()
{
    // free the gray stack array if it was allocated
    if (vm.grayStack != NULL) {
        reallocate(vm.grayStack, sizeof(Obj*) * vm.grayCapacity, 0);
        vm.grayStack = NULL;
        vm.grayCount = 0;
        vm.grayCapacity = 0;
    }
    vm.grayStack = NULL;
    while (vm.gcPhase != GC_PHASE_IDLE) {
        gcStep();
    }
    // drain any remaining gray objects (mark all as unmarked, then sweep)
    vm.grayCount = 0;
    collectGarbage();
    freeTable(&vm.globals);
    freeTable(&vm.strings);
}

void gcWriteBarrier(Value value) {
    if ((vm.gcPhase == GC_PHASE_MARK || vm.gcPhase == GC_PHASE_ATOMIC)
        && IS_OBJ(value)) {
        markObject(AS_OBJ(value));
    }
}

void gcStep(void)
{
    switch (vm.gcPhase) {
        case GC_PHASE_IDLE:
            return;

        case GC_PHASE_MARK:
        case GC_PHASE_ATOMIC:
            while (vm.grayCount > 0) {
                blackenObject(vm.grayStack[--vm.grayCount]);
            }
            while (vm.grayagain != NULL) {
                ObjTable* t = vm.grayagain;
                vm.grayagain = t->gclist;
                t->gclist = NULL;
                growGrayStack();
                vm.grayStack[vm.grayCount++] = (Obj*)t;
            }
            while (vm.grayCount > 0) {
                blackenObject(vm.grayStack[--vm.grayCount]);
            }
            vm.otherWhite = vm.currentWhite;
            vm.currentWhite ^= 1;
            vm.gcPhase = GC_PHASE_SWEEP;
            vm.sweepObj = vm.objects;
            vm.sweepPrev = NULL;
            break;

        case GC_PHASE_SWEEP:
        {
            size_t swept = 0;
            Obj* previous = vm.sweepPrev;
            Obj* object = vm.sweepObj;

            while (object != NULL && swept < vm.marksPerStep) {
                if ((object->marked & GC_COLOR_MASK) != vm.otherWhite) {
                    object->marked = (object->marked & ~GC_COLOR_MASK) | vm.currentWhite;
                    previous = object;
                    object = object->next;
                } else {
                    Obj* unreached = object;
                    object = object->next;

                    if (previous != NULL) {
                        previous->next = object;
                    } else {
                        vm.objects = object;
                    }

                    freeObject(unreached);
                }
                swept++;
            }

            vm.sweepObj = object;
            vm.sweepPrev = previous;

            if (object == NULL) {
                vm.gcPhase = GC_PHASE_IDLE;
                vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
                if (vm.newObjects != NULL) {
                    Obj* newHead = vm.newObjects;
                    vm.newObjects = NULL;
                    Obj* tail = newHead;
                    while (tail->next != NULL) tail = tail->next;
                    tail->next = vm.objects;
                    vm.objects = newHead;
                }
                for (Obj* o = vm.objects; o != NULL; o = o->next) {
                    if (o->type == OBJ_FUNCTION) {
                        Chunk* c = &((ObjFunction*)o)->chunk;
                        if (c->caches) {
                            for (int i = 0; i < c->count; i++) c->caches[i].valid = false;
                        }
                    }
                }
#ifdef DEBUG_LOG_GC
                printf("-- gc end\n");
#endif
            }
            break;
        }
    }
}

// find or create an upvalue pointing to a stack slot
static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;

    // walk the sorted list to find insertion point
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    // already have an upvalue for this slot? reuse it
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

// close upvalues for stack slots that are going out of scope
static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        gcWriteBarrier(upvalue->closed);
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

InterpretResult run(int baseFrame);

static Value findBinaryMetamethod(Value a, Value b, ObjString* name) {
    Value handler = NIL_VAL;
    if (IS_TABLE(a) && AS_TABLE(a)->metatable != NULL) {
        tableGet(&AS_TABLE(a)->metatable->fields, OBJ_VAL((Obj*)name), &handler);
    }
    if (IS_NIL(handler) && IS_TABLE(b) && AS_TABLE(b)->metatable != NULL) {
        tableGet(&AS_TABLE(b)->metatable->fields, OBJ_VAL((Obj*)name), &handler);
    }
    return handler;
}

static bool callBinaryMetamethod(Value handler, Value a, Value b,
                                 Value* scratch, Value* out) {
    ObjClosure* closure;
    if (IS_CLOSURE(handler)) {
        closure = AS_CLOSURE(handler);
    } else if (IS_FUNCTION(handler)) {
        closure = newClosure(AS_FUNCTION(handler));
        for (int i = 0; i < closure->function->upvalueCount; i++) {
            closure->upvalues[i] = NULL;
        }
    } else {
        runtimeError("Metamethod must be a function.");
        return false;
    }
    if (closure->function->arity != 2) {
        runtimeError("Binary metamethod must take 2 arguments.");
        return false;
    }
    if (vm.frameCount >= FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    scratch[0] = handler;
    scratch[1] = a;
    scratch[2] = b;

    int base = vm.frameCount;
    CallFrame* mf = &vm.frames[vm.frameCount++];
    mf->closure = closure;
    mf->ip = closure->function->chunk.code;
    mf->slots = scratch;

    InterpretResult r = run(base);
    if (r != INTERPRET_OK) return false;

    *out = scratch[0];
    return true;
}

InterpretResult run(int baseFrame)
{

    register CallFrame* frame = &vm.frames[vm.frameCount - 1];
#define FRAME (*frame)
#define RELOAD_FRAME() (frame = &vm.frames[vm.frameCount - 1])
#define READ_INST() (*FRAME.ip++)
#define READ_CONSTANT(inst) (FRAME.closure->function->chunk.constants.values[GET_Bx(inst)])
#define REG(index) (FRAME.slots[index])

// binary op macro - checks types and applies the operator
#define IS_NUMERIC(v) (IS_NUMBER(v) || IS_INTEGER(v))

#define STORE_INT(dst, val) do { \
    int64_t _v = (val); \
    if (_v < INT48_MIN || _v > INT48_MAX) { \
        REG(dst) = NUMBER_VAL((double)_v); \
    } else { \
        REG(dst) = INTEGER_VAL(_v); \
    } \
} while (false)

#define GC_STEP() do { \
    if (vm.gcPhase != GC_PHASE_IDLE) gcStep(); \
} while (0)

#ifdef __GNUC__
#define DISPATCH()      do { instruction = READ_INST(); goto *dispatch[GET_OP(instruction)]; } while (0)
#define DISPATCH_POLL() do { GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)]; } while (0)
#endif

#define GC_WRITE_BARRIER(val) do { \
    if ((vm.gcPhase == GC_PHASE_MARK || vm.gcPhase == GC_PHASE_ATOMIC) \
        && IS_OBJ(val)) { \
        markObject(AS_OBJ(val)); \
    } \
} while (0)

#define REG_SET(r, val) do { \
    Value _v = (val); \
    GC_WRITE_BARRIER(_v); \
    FRAME.slots[(r)] = _v; \
} while (0)

#ifdef __GNUC__
#define TRY_BINARY_META(mmName) do { \
    Value _h = findBinaryMetamethod(av, bv, (mmName)); \
    if (!IS_NIL(_h)) { \
        Value* _scratch = FRAME.slots + FRAME.closure->function->maxRegs; \
        Value _res; \
        if (!callBinaryMetamethod(_h, av, bv, _scratch, &_res)) \
            return INTERPRET_RUNTIME_ERROR; \
        REG_SET(GET_A(inst), _res); \
        DISPATCH_POLL(); \
    } \
} while (0)
#else
#define TRY_BINARY_META(mmName) do {} while (0)
#endif

#define BINARY_OP(op)                                                \
    do                                                               \
    {                                                                \
        uint32_t inst = FRAME.ip[-1];                                \
        Value bv = REG(GET_C(inst));                                 \
        Value av = REG(GET_B(inst));                                 \
        if (IS_NUMERIC(av) && IS_NUMERIC(bv)) {                          \
            double da = IS_INTEGER(av) ? (double)AS_INTEGER(av) : valueToNumber(av); \
            double db = IS_INTEGER(bv) ? (double)AS_INTEGER(bv) : valueToNumber(bv); \
            REG_SET(GET_A(inst), NUMBER_VAL(da op db));              \
        } else {                                                     \
            runtimeError("Operands must be numbers.");               \
            return INTERPRET_RUNTIME_ERROR;                          \
        }                                                            \
    } while (false)

#ifdef __GNUC__
    static void* dispatch[] = {
        [OP_CONSTANT] = &&OP_CONSTANT,
        [OP_DEFINE_GLOBAL] = &&OP_DEFINE_GLOBAL,
        [OP_GET_GLOBAL] = &&OP_GET_GLOBAL,
        [OP_SET_GLOBAL] = &&OP_SET_GLOBAL,
        [OP_GET_UPVALUE] = &&OP_GET_UPVALUE,
        [OP_SET_UPVALUE] = &&OP_SET_UPVALUE,
        [OP_PRINT] = &&OP_PRINT,
        [OP_ADD] = &&OP_ADD,
        [OP_SUBTRACT] = &&OP_SUBTRACT,
        [OP_MULTIPLY] = &&OP_MULTIPLY,
        [OP_DIVIDE] = &&OP_DIVIDE,
        [OP_MODULO] = &&OP_MODULO,
        [OP_NEGATE] = &&OP_NEGATE,
        [OP_EQUAL] = &&OP_EQUAL,
        [OP_GREATER] = &&OP_GREATER,
        [OP_LESS] = &&OP_LESS,
        [OP_GREATER_EQUAL] = &&OP_GREATER_EQUAL,
        [OP_LESS_EQUAL] = &&OP_LESS_EQUAL,
        [OP_NOT] = &&OP_NOT,
        [OP_BREAK] = &&OP_BREAK,
        [OP_CONTINUE] = &&OP_CONTINUE,
        [OP_TABLE] = &&OP_TABLE,
        [OP_GET_TABLE] = &&OP_GET_TABLE,
        [OP_SET_TABLE] = &&OP_SET_TABLE,
        [OP_LENGTH] = &&OP_LENGTH,
        [OP_SET_METATABLE] = &&OP_SET_METATABLE,
        [OP_GET_METATABLE] = &&OP_GET_METATABLE,
        [OP_MOVE] = &&OP_MOVE,
        [OP_NIL] = &&OP_NIL,
        [OP_TRUE] = &&OP_TRUE,
        [OP_FALSE] = &&OP_FALSE,
        [OP_JUMP] = &&OP_JUMP,
        [OP_JUMP_IF_FALSE] = &&OP_JUMP_IF_FALSE,
        [OP_POP] = &&OP_POP,
        [OP_CALL] = &&OP_CALL,
        [OP_RETURN] = &&OP_RETURN,
        [OP_CLOSURE] = &&OP_CLOSURE,
        [OP_GET_READONLY_UPVALUE] = &&OP_GET_READONLY_UPVALUE,
        [OP_CLOCK] = &&OP_CLOCK,
        [OP_INCREMENT] = &&OP_INCREMENT,
        [OP_NOP] = &&OP_NOP,
        [OP_INT_ADD] = &&OP_INT_ADD,
        [OP_INT_SUBTRACT] = &&OP_INT_SUBTRACT,
        [OP_INT_MULTIPLY] = &&OP_INT_MULTIPLY,
        [OP_INT_LESS] = &&OP_INT_LESS,
        [OP_INT_GREATER] = &&OP_INT_GREATER,
        [OP_INT_LESS_EQUAL] = &&OP_INT_LESS_EQUAL,
        [OP_INT_GREATER_EQUAL] = &&OP_INT_GREATER_EQUAL,
        [OP_INT_EQUAL] = &&OP_INT_EQUAL,
        [OP_INT_NEGATE] = &&OP_INT_NEGATE,
        [OP_INT_INCREMENT] = &&OP_INT_INCREMENT,
        [OP_INT_MODULO] = &&OP_INT_MODULO,
        [OP_INT_JLT] = &&OP_INT_JLT,
        [OP_INT_JLE] = &&OP_INT_JLE,
        [OP_INT_JGT] = &&OP_INT_JGT,
        [OP_INT_JGE] = &&OP_INT_JGE,
        [OP_INT_JE] = &&OP_INT_JE,
        [OP_NOT_EQUAL] = &&OP_NOT_EQUAL,
[OP_SQRT] = &&OP_SQRT,
        [OP_FOR_IN] = &&OP_FOR_IN,
    };

    uint32_t instruction;
    GC_STEP();
    instruction = READ_INST();
    goto *dispatch[GET_OP(instruction)];

    for (;;) {
#else
    for (;;) {
        GC_STEP();
        uint32_t instruction = READ_INST();
        switch (GET_OP(instruction)) {
#endif

#ifdef __GNUC__
        OP_CONSTANT:
#else
        case OP_CONSTANT:
#endif
        {
            Value constant = READ_CONSTANT(instruction);
            if (IS_FUNCTION(constant)) {
                ObjFunction* function = AS_FUNCTION(constant);
                ObjClosure* closure = newClosure(function);
                REG(GET_A(instruction)) = OBJ_VAL(closure);

                for (int i = 0; i < function->upvalueCount; i++) {
                    uint8_t index = function->upvalues[i].index;
                    if (function->upvalues[i].isLocal) {
                        closure->upvalues[i] = captureUpvalue(&REG(index));
                    } else {
                        closure->upvalues[i] = FRAME.closure->upvalues[index];
                    }
                }
            } else {
                REG(GET_A(instruction)) = constant;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_ADD:
#else
        case OP_ADD:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value bv = REG(GET_C(inst));
            Value av = REG(GET_B(inst));
            if (IS_INTEGER(av) && IS_INTEGER(bv)) {
                int64_t ia = AS_INTEGER(av);
                int64_t ib = AS_INTEGER(bv);
                int64_t result;
                if (__builtin_add_overflow(ia, ib, &result)) {
                    REG_SET(GET_A(inst), NUMBER_VAL((double)ia + (double)ib));
                } else {
                    STORE_INT(GET_A(inst), result);
                }
            } else if (IS_NUMBER(av) && IS_NUMBER(bv)) {
                REG_SET(GET_A(inst), numberToValue(valueToNumber(av) + valueToNumber(bv)));
            } else if (IS_INTEGER(av) && IS_NUMBER(bv)) {
                REG_SET(GET_A(inst), NUMBER_VAL((double)AS_INTEGER(av) + valueToNumber(bv)));
            } else if (IS_NUMBER(av) && IS_INTEGER(bv)) {
                REG_SET(GET_A(inst), NUMBER_VAL(valueToNumber(av) + (double)AS_INTEGER(bv)));
            } else if (IS_STRING(av) && IS_STRING(bv)) {
                ObjString* strA = AS_STRING(av);
                ObjString* strB = AS_STRING(bv);
                int length = strA->length + strB->length;
                char* chars = (char*)reallocate(NULL, 0, length + 1);
                memcpy(chars, strA->chars, strA->length);
                memcpy(chars + strA->length, strB->chars, strB->length);
                chars[length] = '\0';
                REG_SET(GET_A(inst), OBJ_VAL(takeString(chars, length)));
            } else if (IS_NUMERIC(av) && IS_STRING(bv)) {
                char numStr[64];
                double numVal = AS_NUMBER(av);
                int numLen;
                if (numVal == (double)(int64_t)numVal) {
                    numLen = snprintf(numStr, sizeof(numStr), "%.0f", numVal);
                } else {
                    numLen = snprintf(numStr, sizeof(numStr), "%g", numVal);
                }
                ObjString* strB = AS_STRING(bv);
                int length = numLen + strB->length;
                char* chars = (char*)reallocate(NULL, 0, length + 1);
                memcpy(chars, numStr, numLen);
                memcpy(chars + numLen, strB->chars, strB->length);
                chars[length] = '\0';
                REG_SET(GET_A(inst), OBJ_VAL(takeString(chars, length)));
            } else if (IS_STRING(av) && IS_NUMERIC(bv)) {
                char numStr[64];
                double numVal = AS_NUMBER(bv);
                int numLen;
                if (numVal == (double)(int64_t)numVal) {
                    numLen = snprintf(numStr, sizeof(numStr), "%.0f", numVal);
                } else {
                    numLen = snprintf(numStr, sizeof(numStr), "%g", numVal);
                }
                ObjString* strA = AS_STRING(av);
                int length = strA->length + numLen;
                char* chars = (char*)reallocate(NULL, 0, length + 1);
                memcpy(chars, strA->chars, strA->length);
                memcpy(chars + strA->length, numStr, numLen);
                chars[length] = '\0';
                REG_SET(GET_A(inst), OBJ_VAL(takeString(chars, length)));
            } else {
                TRY_BINARY_META(vm.mmAdd);
                runtimeError("Operands must be numbers or strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_SUBTRACT:
        {
            uint32_t inst = FRAME.ip[-1];
            Value bv = REG(GET_C(inst));
            Value av = REG(GET_B(inst));
            if (IS_INTEGER(av) && IS_INTEGER(bv)) {
                int64_t ia = AS_INTEGER(av);
                int64_t ib = AS_INTEGER(bv);
                int64_t result;
                if (__builtin_sub_overflow(ia, ib, &result)) {
                    REG_SET(GET_A(inst), NUMBER_VAL((double)ia - (double)ib));
                } else {
                    STORE_INT(GET_A(inst), result);
                }
            } else if (IS_NUMERIC(av) && IS_NUMERIC(bv)) {
                double da = IS_INTEGER(av) ? (double)AS_INTEGER(av) : valueToNumber(av);
                double db = IS_INTEGER(bv) ? (double)AS_INTEGER(bv) : valueToNumber(bv);
                REG_SET(GET_A(inst), NUMBER_VAL(da - db));
            } else {
                TRY_BINARY_META(vm.mmSub);
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            DISPATCH_POLL();
        }

        OP_MULTIPLY:
        {
            uint32_t inst = FRAME.ip[-1];
            Value bv = REG(GET_C(inst));
            Value av = REG(GET_B(inst));
            if (IS_INTEGER(av) && IS_INTEGER(bv)) {
                int64_t ia = AS_INTEGER(av);
                int64_t ib = AS_INTEGER(bv);
                int64_t result;
                if (__builtin_mul_overflow(ia, ib, &result)) {
                    REG_SET(GET_A(inst), NUMBER_VAL((double)ia * (double)ib));
                } else {
                    STORE_INT(GET_A(inst), result);
                }
            } else if (IS_NUMERIC(av) && IS_NUMERIC(bv)) {
                double da = IS_INTEGER(av) ? (double)AS_INTEGER(av) : valueToNumber(av);
                double db = IS_INTEGER(bv) ? (double)AS_INTEGER(bv) : valueToNumber(bv);
                REG_SET(GET_A(inst), NUMBER_VAL(da * db));
            } else {
                TRY_BINARY_META(vm.mmMul);
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            DISPATCH_POLL();
        }

        OP_DIVIDE:
        {
            uint32_t inst = FRAME.ip[-1];
            Value bv = REG(GET_C(inst));
            Value av = REG(GET_B(inst));
            if (IS_NUMERIC(av) && IS_NUMERIC(bv)) {
                double da = IS_INTEGER(av) ? (double)AS_INTEGER(av) : valueToNumber(av);
                double db = IS_INTEGER(bv) ? (double)AS_INTEGER(bv) : valueToNumber(bv);
                REG_SET(GET_A(inst), NUMBER_VAL(da / db));
            } else {
                TRY_BINARY_META(vm.mmDiv);
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            DISPATCH_POLL();
        }

        OP_MODULO:
#else
        case OP_SUBTRACT:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                int64_t result;
                if (__builtin_sub_overflow(AS_INTEGER(a), AS_INTEGER(b), &result)) {
                    REG(GET_A(inst)) = NUMBER_VAL((double)AS_INTEGER(a) - (double)AS_INTEGER(b));
                } else {
                    STORE_INT(GET_A(inst), result);
                }
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = NUMBER_VAL(AS_NUMBER(a) - AS_NUMBER(b));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_MULTIPLY:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                int64_t result;
                if (__builtin_mul_overflow(AS_INTEGER(a), AS_INTEGER(b), &result)) {
                    REG(GET_A(inst)) = NUMBER_VAL((double)AS_INTEGER(a) * (double)AS_INTEGER(b));
                } else {
                    STORE_INT(GET_A(inst), result);
                }
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = NUMBER_VAL(AS_NUMBER(a) * AS_NUMBER(b));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_DIVIDE:   BINARY_OP(/); break;
        case OP_MODULO:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                int64_t ib = AS_INTEGER(b);
                if (ib == 0) {
                    runtimeError("Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int64_t ia = AS_INTEGER(a);
                if (ia == INT64_MIN && ib == -1) {
                    REG(GET_A(inst)) = NUMBER_VAL(fmod((double)ia, (double)ib));
                } else {
                    STORE_INT(GET_A(inst), ia % ib);
                }
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = NUMBER_VAL(fmod(AS_NUMBER(a), AS_NUMBER(b)));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_EQUAL:
#else
        case OP_EQUAL:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            bool equal = false;
            if (IS_BOOL(a) && IS_BOOL(b)) {
                equal = AS_BOOL(a) == AS_BOOL(b);
            } else if (IS_NIL(a) && IS_NIL(b)) {
                equal = true;
            } else if (IS_INTEGER(a) && IS_INTEGER(b)) {
                equal = AS_INTEGER(a) == AS_INTEGER(b);
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                equal = AS_NUMBER(a) == AS_NUMBER(b);
            } else if (IS_OBJ(a) && IS_OBJ(b)) {
                equal = AS_OBJ(a) == AS_OBJ(b);
            }
            REG(GET_A(inst)) = BOOL_VAL(equal);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_NOT_EQUAL:
#else
        case OP_NOT_EQUAL:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            bool equal = false;
            if (IS_BOOL(a) && IS_BOOL(b)) {
                equal = AS_BOOL(a) == AS_BOOL(b);
            } else if (IS_NIL(a) && IS_NIL(b)) {
                equal = true;
            } else if (IS_INTEGER(a) && IS_INTEGER(b)) {
                equal = AS_INTEGER(a) == AS_INTEGER(b);
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                equal = AS_NUMBER(a) == AS_NUMBER(b);
            } else if (IS_OBJ(a) && IS_OBJ(b)) {
                equal = AS_OBJ(a) == AS_OBJ(b);
            }
            REG(GET_A(inst)) = BOOL_VAL(!equal);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GREATER:
#else
        case OP_GREATER:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_INTEGER(a) > AS_INTEGER(b));
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) > AS_NUMBER(b));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_LESS:
#else
        case OP_LESS:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_INTEGER(a) < AS_INTEGER(b));
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) < AS_NUMBER(b));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GREATER_EQUAL:
#else
        case OP_GREATER_EQUAL:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_INTEGER(a) >= AS_INTEGER(b));
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) >= AS_NUMBER(b));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_LESS_EQUAL:
#else
        case OP_LESS_EQUAL:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_INTEGER(a) && IS_INTEGER(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_INTEGER(a) <= AS_INTEGER(b));
            } else if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
                REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) <= AS_NUMBER(b));
            } else {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_NOT:
#else
        case OP_NOT:
#endif
        {
            uint32_t inst = FRAME.ip[-1];
            Value val = REG(GET_B(inst));
            bool result;
            if (IS_BOOL(val)) {
                result = !AS_BOOL(val);
            } else {
                result = IS_NIL(val);
            }
            REG(GET_A(inst)) = BOOL_VAL(result);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_TABLE:
#else
        case OP_TABLE:
#endif
        {
            ObjTable* table = newTable();
            REG(GET_A(instruction)) = OBJ_VAL(table);
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_LENGTH:
#else
        case OP_LENGTH:
#endif
        {
            uint8_t dest = GET_A(instruction);
            uint8_t src = GET_B(instruction);
            Value val = REG(src);
            if (IS_STRING(val)) {
                REG_SET(dest, INTEGER_VAL(AS_STRING(val)->length));
            } else if (IS_TABLE(val)) {
                ObjTable* table = AS_TABLE(val);
                if (table->metatable != NULL) {
                    ObjTable* mt = table->metatable;
                    if (mt->metaGen == mt->writeGen && !IS_NIL(mt->cachedLen)) {
                    } else {
                        Value lenValue;
                        if (tableGet(&mt->fields, OBJ_VAL((Obj*)vm.mmLen), &lenValue)) {
                            mt->cachedLen = lenValue;
                            mt->metaGen = mt->writeGen;
                        }
                    }
                }
                int len = table->arrayCapacity;
                while (len > 0 && IS_NIL(table->array[len - 1])) {
                    len--;
                }
                REG_SET(dest, NUMBER_VAL((double)len));
            } else {
                runtimeError("Operand must be a table.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_FOR_IN:
#else
        case OP_FOR_IN:
#endif
        {
            uint8_t tableReg = GET_A(instruction);
            int keyReg = tableReg + 1;
            int valueReg = tableReg + 2;
            int cursorReg = tableReg + 3;
            int32_t offset = GET_Bx(instruction);

            if (!IS_TABLE(REG(tableReg))) {
                runtimeError("Can only iterate over tables.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjTable* table = AS_TABLE(REG(tableReg));

            int64_t cursor = 0;
            Value curVal = REG(cursorReg);
            if (IS_INTEGER(curVal)) cursor = AS_INTEGER(curVal);

            for (;;) {
                if (cursor < (int64_t)table->arrayCapacity) {
                    Value v = table->array[cursor];
                    if (IS_NIL(v)) {
                        cursor++;
                        continue;
                    }
                    REG_SET(keyReg, INTEGER_VAL(cursor + 1));
                    REG_SET(valueReg, v);
                    REG_SET(cursorReg, INTEGER_VAL(cursor + 1));
                    break;
                }
                int64_t e = cursor - (int64_t)table->arrayCapacity;
                if (e >= (int64_t)table->fields.capacity) {
                    FRAME.ip += offset;
                    break;
                }
                Entry* entry = &table->fields.entries[e];
                if (IS_NIL(entry->key)) {
                    cursor++;
                    continue;
                }
                REG_SET(keyReg, entry->key);
                REG_SET(valueReg, entry->value);
                REG_SET(cursorReg, INTEGER_VAL(cursor + 1));
                break;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_SET_METATABLE:
#else
        case OP_SET_METATABLE:
#endif
        {
            uint8_t tableReg = GET_A(instruction);
            uint8_t mtReg = GET_B(instruction);
            Value tableVal = REG(tableReg);
            Value mtVal = REG(mtReg);
            if (!IS_TABLE(tableVal)) {
                runtimeError("First argument must be a table.");
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjTable* table = AS_TABLE(tableVal);
            if (IS_TABLE(mtVal)) {
                gcWriteBarrier(mtVal);
                table->metatable = AS_TABLE(mtVal);
            } else if (IS_NIL(mtVal)) {
                table->metatable = NULL;
            } else {
                runtimeError("Metatable must be a table or nil.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GET_METATABLE:
#else
        case OP_GET_METATABLE:
#endif
        {
            uint8_t dest = GET_A(instruction);
            uint8_t src = GET_B(instruction);
            Value val = REG(src);
            if (IS_TABLE(val)) {
                ObjTable* table = AS_TABLE(val);
                if (table->metatable != NULL) {
                    REG_SET(dest, OBJ_VAL(table->metatable));
                } else {
                    REG_SET(dest, NIL_VAL);
                }
            } else {
                runtimeError("Operand must be a table.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GET_TABLE:
#else
        case OP_GET_TABLE:
#endif
        {
            uint8_t dest = GET_A(instruction);
            uint8_t tableReg = GET_B(instruction);
            uint8_t keyReg = GET_C(instruction);
            Value tableVal = REG(tableReg);
            Value keyVal = REG(keyReg);

            // Inline cache check (per-chunk, indexed by instruction position)
            Chunk* chunk = &FRAME.closure->function->chunk;
            int instIdx = (int)(FRAME.ip - 1 - chunk->code);
            InlineCache* ic = &chunk->caches[instIdx];
            if (ic->valid && ic->table == tableVal && ic->key == keyVal)
            {
                if (ic->tableGen == AS_TABLE(tableVal)->writeGen) {
                    REG_SET(dest, ic->result);
#ifdef __GNUC__
                    DISPATCH_POLL();
#else
                    break;
#endif
                }
            }

            if (!IS_TABLE(tableVal)) {
                runtimeError("Can only index into tables.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjTable* table = AS_TABLE(tableVal);
            Value result;
            bool found = false;

            if (IS_INTEGER(keyVal)) {
                int64_t index = AS_INTEGER(keyVal);
                if (index >= 1 && index <= table->arrayCapacity) {
                    result = table->array[index - 1];
                    found = !IS_NIL(result);
                }
                if (!found) {
                    found = objTableGet(table, keyVal, &result);
                }
            } else if (IS_NUMBER(keyVal)) {
                found = objTableGet(table, keyVal, &result);
            } else if (IS_STRING(keyVal)) {
                found = objTableGet(table, keyVal, &result);
            } else {
                runtimeError("Table key must be a number or string.");
                return INTERPRET_RUNTIME_ERROR;
            }

            if (!found && table->metatable != NULL) {
                ObjTable* mt = table->metatable;
                Value indexValue = NIL_VAL;
                if (mt->metaGen == mt->writeGen && !IS_NIL(mt->cachedIndex)) {
                    indexValue = mt->cachedIndex;
                } else {
                    if (tableGet(&mt->fields, OBJ_VAL((Obj*)vm.mmIndex), &indexValue)) {
                        mt->cachedIndex = indexValue;
                        mt->metaGen = mt->writeGen;
                    }
                }
                if (!IS_NIL(indexValue)) {
                    if (IS_TABLE(indexValue)) {
                        ObjTable* idxTable = AS_TABLE(indexValue);
                        if (IS_INTEGER(keyVal)) {
                            int64_t index = AS_INTEGER(keyVal);
                            if (index >= 1 && index <= idxTable->arrayCapacity) {
                                result = idxTable->array[index - 1];
                                found = !IS_NIL(result);
                            }
                            if (!found) {
                                found = objTableGet(idxTable, keyVal, &result);
                            }
                        } else if (IS_NUMBER(keyVal)) {
                            found = objTableGet(idxTable, keyVal, &result);
                        } else if (IS_STRING(keyVal)) {
                            found = objTableGet(idxTable, keyVal, &result);
                        }
                    } else if (IS_CLOSURE(indexValue) || IS_FUNCTION(indexValue)) {
                        result = indexValue;
                        found = true;
                    }
                }
            }

            if (!found) {
                result = NIL_VAL;
            }

            REG_SET(dest, result);
            ic->valid = true;
            ic->table = tableVal;
            ic->tableGen = table->writeGen;
            ic->key = keyVal;
            ic->result = result;
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_SET_TABLE:
#else
        case OP_SET_TABLE:
#endif
        {
            uint8_t tableReg = GET_A(instruction);
            uint8_t keyReg = GET_B(instruction);
            uint8_t valReg = GET_C(instruction);
            Value tableVal = REG(tableReg);
            Value keyVal = REG(keyReg);
            Value value = REG(valReg);

            if (!IS_TABLE(tableVal)) {
                runtimeError("Can only index into tables.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjTable* table = AS_TABLE(tableVal);

            bool keyExists = false;
            if (IS_INTEGER(keyVal)) {
                int64_t index = AS_INTEGER(keyVal);
                if (index >= 1 && index <= table->arrayCapacity && !IS_NIL(table->array[index - 1])) {
                    keyExists = true;
                }
                if (!keyExists) {
                    Value dummy;
                    keyExists = tableGet(&table->fields, keyVal, &dummy);
                }
            } else if (IS_NUMBER(keyVal)) {
                Value dummy;
                keyExists = tableGet(&table->fields, keyVal, &dummy);
            } else if (IS_STRING(keyVal)) {
                Value dummy;
                keyExists = tableGet(&table->fields, keyVal, &dummy);
            }

            if (!keyExists && table->metatable != NULL) {
                ObjTable* mt = table->metatable;
                Value newIndexValue = NIL_VAL;
                if (mt->metaGen == mt->writeGen && !IS_NIL(mt->cachedNewIndex)) {
                    newIndexValue = mt->cachedNewIndex;
                } else {
                    if (tableGet(&mt->fields, OBJ_VAL((Obj*)vm.mmNewIndex), &newIndexValue)) {
                        mt->cachedNewIndex = newIndexValue;
                        mt->metaGen = mt->writeGen;
                    }
                }
                if (!IS_NIL(newIndexValue)) {
                    if (IS_TABLE(newIndexValue)) {
                        ObjTable* target = AS_TABLE(newIndexValue);
                        if (IS_INTEGER(keyVal)) {
                            int64_t index = AS_INTEGER(keyVal);
                            if (index >= 1 && index <= target->arrayCapacity) {
                                GC_WRITE_BARRIER(value);
                                target->array[index - 1] = value;
                            } else {
                                tableSet(&target->fields, keyVal, value);
                            }
                        } else if (IS_NUMBER(keyVal)) {
                            tableSet(&target->fields, keyVal, value);
                        } else if (IS_STRING(keyVal)) {
                            tableSet(&target->fields, keyVal, value);
                        }
#ifdef __GNUC__
                        DISPATCH_POLL();
#else
                        break;
#endif
                    } else if (IS_CLOSURE(newIndexValue) || IS_FUNCTION(newIndexValue)) {
                    }
                }
            }

            table->writeGen++;

            if (IS_INTEGER(keyVal)) {
                int64_t index = AS_INTEGER(keyVal);
                if (index < 1) {
                    runtimeError("Table index must be >= 1.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (index <= table->arrayCapacity) {
                    GC_WRITE_BARRIER(value);
                    table->array[index - 1] = value;
                } else {
                    int newCap = table->arrayCapacity * 2 + 4;
                    if (index <= newCap || table->arrayCapacity < 64) {
                        if (newCap < index) newCap = (int)index;
                        table->array = reallocate(table->array, sizeof(Value) * table->arrayCapacity, sizeof(Value) * newCap);
                        for (int i = table->arrayCapacity; i < newCap; i++) {
                            table->array[i] = NIL_VAL;
                        }
                        table->arrayCapacity = newCap;
                        GC_WRITE_BARRIER(value);
                        table->array[index - 1] = value;
                    } else {
                        tableSet(&table->fields, keyVal, value);
                    }
                }
            } else if (IS_NUMBER(keyVal) || IS_STRING(keyVal)) {
                tableSet(&table->fields, keyVal, value);
            } else {
                runtimeError("Table key must be a number or string.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_BREAK:
#else
        case OP_BREAK:
#endif
        {
            uint16_t offset = GET_Bx(instruction);
            FRAME.ip += offset;
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_CONTINUE:
#else
        case OP_CONTINUE:
#endif
        {
            int16_t offset = (int16_t)GET_Bx(instruction);
            FRAME.ip += offset;
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_NEGATE:
#else
        case OP_NEGATE:
#endif
        {
            Value val = REG(GET_B(instruction));
            if (IS_INTEGER(val)) {
                STORE_INT(GET_A(instruction), -AS_INTEGER(val));
            } else if (IS_NUMBER(val)) {
                REG(GET_A(instruction)) = NUMBER_VAL(-AS_NUMBER(val));
            } else {
                runtimeError("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_CLOCK:
#else
        case OP_CLOCK:
#endif
        {
#ifdef _WIN32
            LARGE_INTEGER freq, counter;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&counter);
            REG(GET_A(instruction)) = NUMBER_VAL((double)counter.QuadPart / (double)freq.QuadPart);
#else
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            REG(GET_A(instruction)) = NUMBER_VAL(ts.tv_sec + ts.tv_nsec / 1e9);
#endif
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INCREMENT:
#else
        case OP_INCREMENT:
#endif
        {
            Value val = REG(GET_A(instruction));
            if (IS_INTEGER(val)) {
                int64_t v = AS_INTEGER(val);
                STORE_INT(GET_A(instruction), v + 1);
            } else if (IS_NUMBER(val)) {
                REG(GET_A(instruction)) = NUMBER_VAL(valueToNumber(val) + 1.0);
            } else {
                runtimeError("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_PRINT:
#else
        case OP_PRINT:
#endif
        {
            printValue(REG(GET_A(instruction)));
            printf("\n");
            fflush(stdout);
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_DEFINE_GLOBAL:
#else
        case OP_DEFINE_GLOBAL:
#endif
        {
            ObjString *name = AS_STRING(READ_CONSTANT(instruction));
            tableSet(&vm.globals, OBJ_VAL((Obj*)name), REG(GET_A(instruction)));
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GET_GLOBAL:
#else
        case OP_GET_GLOBAL:
#endif
        {
            ObjString *name = AS_STRING(READ_CONSTANT(instruction));
            GlobalCacheEntry* gc = &vm.globalCache[(name->hash) & (GLOBAL_CACHE_SIZE - 1)];
            if (gc->name == name && gc->generation == vm.globals.generation) {
                REG(GET_A(instruction)) = gc->entry->value;
#ifdef __GNUC__
                DISPATCH_POLL();
#else
                break;
#endif
            }
            Entry* entry = tableGetEntry(&vm.globals, OBJ_VAL((Obj*)name));
            if (entry == NULL)
            {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            gc->name = name;
            gc->generation = vm.globals.generation;
            gc->entry = entry;
            REG(GET_A(instruction)) = entry->value;
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_SET_GLOBAL:
#else
        case OP_SET_GLOBAL:
#endif
        {
            ObjString *name = AS_STRING(READ_CONSTANT(instruction));
            Value newVal = REG(GET_A(instruction));
            GlobalCacheEntry* gc = &vm.globalCache[(name->hash) & (GLOBAL_CACHE_SIZE - 1)];
            if (gc->name == name && gc->generation == vm.globals.generation) {
                gc->entry->value = newVal;
                gcWriteBarrier(newVal);
#ifdef __GNUC__
                DISPATCH_POLL();
#else
                break;
#endif
            }
            Entry* entry = tableGetEntry(&vm.globals, OBJ_VAL((Obj*)name));
            if (entry == NULL)
            {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            gc->name = name;
            gc->generation = vm.globals.generation;
            gc->entry = entry;
            entry->value = newVal;
            gcWriteBarrier(newVal);
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GET_UPVALUE:
#else
        case OP_GET_UPVALUE:
#endif
        {
            uint8_t reg = GET_A(instruction);
            uint8_t slot = GET_B(instruction);
            REG_SET(reg, *FRAME.closure->upvalues[slot]->location);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_GET_READONLY_UPVALUE:
#else
        case OP_GET_READONLY_UPVALUE:
#endif
        {
            uint8_t reg = GET_A(instruction);
            uint8_t slot = GET_B(instruction);
            REG_SET(reg, FRAME.closure->readonlyValues[slot]);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_SET_UPVALUE:
#else
        case OP_SET_UPVALUE:
#endif
        {
            int slot = GET_A(instruction);
            Value upvVal = REG(GET_B(instruction));
            GC_WRITE_BARRIER(upvVal);
            *FRAME.closure->upvalues[slot]->location = upvVal;
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_RETURN:
#else
        case OP_RETURN:
#endif
        {
            Value result = REG(GET_A(instruction));

            closeUpvalues(FRAME.slots);

            FRAME.slots[0] = result;
            GC_WRITE_BARRIER(result);

            vm.frameCount--;
            if (vm.frameCount == baseFrame)
            {
                return INTERPRET_OK;
            }
            RELOAD_FRAME();
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_MOVE:
#else
        case OP_MOVE:
#endif
        {
            uint8_t dest = GET_A(instruction);
            uint8_t src = GET_B(instruction);
            REG_SET(dest, REG(src));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_TRUE:
#else
        case OP_TRUE:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(true);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_FALSE:
#else
        case OP_FALSE:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(false);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_NIL:
#else
        case OP_NIL:
#endif
        {
            REG(GET_A(instruction)) = NIL_VAL;
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_JUMP:
#else
        case OP_JUMP:
#endif
        {
            int16_t offset = (int16_t)GET_Bx(instruction);
            FRAME.ip += offset;
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_JUMP_IF_FALSE:
#else
        case OP_JUMP_IF_FALSE:
#endif
        {
            uint8_t reg = GET_A(instruction);
            uint16_t offset = GET_Bx(instruction);
            Value val = REG(reg);
            bool isFalse = IS_BOOL(val) ? !AS_BOOL(val) : IS_NIL(val);
            if (isFalse) {
                FRAME.ip += offset;
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_NOP:
#else
        case OP_NOP:
#endif
        {
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_ADD:
#else
        case OP_INT_ADD:
#endif
        {
            int64_t bv = AS_INTEGER(REG(GET_B(instruction)));
            int64_t cv = AS_INTEGER(REG(GET_C(instruction)));
            STORE_INT(GET_A(instruction), bv + cv);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_SUBTRACT:
#else
        case OP_INT_SUBTRACT:
#endif
        {
            int64_t bv = AS_INTEGER(REG(GET_B(instruction)));
            int64_t cv = AS_INTEGER(REG(GET_C(instruction)));
            STORE_INT(GET_A(instruction), bv - cv);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_MULTIPLY:
#else
        case OP_INT_MULTIPLY:
#endif
        {
            int64_t bv = AS_INTEGER(REG(GET_B(instruction)));
            int64_t cv = AS_INTEGER(REG(GET_C(instruction)));
            STORE_INT(GET_A(instruction), bv * cv);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_LESS:
#else
        case OP_INT_LESS:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(
                AS_INTEGER(REG(GET_B(instruction))) <
                AS_INTEGER(REG(GET_C(instruction))));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_GREATER:
#else
        case OP_INT_GREATER:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(
                AS_INTEGER(REG(GET_B(instruction))) >
                AS_INTEGER(REG(GET_C(instruction))));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_LESS_EQUAL:
#else
        case OP_INT_LESS_EQUAL:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(
                AS_INTEGER(REG(GET_B(instruction))) <=
                AS_INTEGER(REG(GET_C(instruction))));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_GREATER_EQUAL:
#else
        case OP_INT_GREATER_EQUAL:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(
                AS_INTEGER(REG(GET_B(instruction))) >=
                AS_INTEGER(REG(GET_C(instruction))));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_EQUAL:
#else
        case OP_INT_EQUAL:
#endif
        {
            REG(GET_A(instruction)) = BOOL_VAL(
                AS_INTEGER(REG(GET_B(instruction))) ==
                AS_INTEGER(REG(GET_C(instruction))));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_JLT:
#else
        case OP_INT_JLT:
#endif
        {
            if (!(AS_INTEGER(REG(GET_A(instruction))) < AS_INTEGER(REG(GET_B(instruction))))) {
                FRAME.ip += (int8_t)GET_C(instruction);
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_JLE:
#else
        case OP_INT_JLE:
#endif
        {
            if (!(AS_INTEGER(REG(GET_A(instruction))) <= AS_INTEGER(REG(GET_B(instruction))))) {
                FRAME.ip += (int8_t)GET_C(instruction);
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_JGT:
#else
        case OP_INT_JGT:
#endif
        {
            if (!(AS_INTEGER(REG(GET_A(instruction))) > AS_INTEGER(REG(GET_B(instruction))))) {
                FRAME.ip += (int8_t)GET_C(instruction);
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_JGE:
#else
        case OP_INT_JGE:
#endif
        {
            if (!(AS_INTEGER(REG(GET_A(instruction))) >= AS_INTEGER(REG(GET_B(instruction))))) {
                FRAME.ip += (int8_t)GET_C(instruction);
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_JE:
#else
        case OP_INT_JE:
#endif
        {
            if (!(AS_INTEGER(REG(GET_A(instruction))) == AS_INTEGER(REG(GET_B(instruction))))) {
                FRAME.ip += (int8_t)GET_C(instruction);
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_NEGATE:
#else
        case OP_INT_NEGATE:
#endif
        {
            STORE_INT(GET_A(instruction),
                -AS_INTEGER(REG(GET_B(instruction))));
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_INCREMENT:
#else
        case OP_INT_INCREMENT:
#endif
        {
            STORE_INT(GET_A(instruction),
                AS_INTEGER(REG(GET_A(instruction))) + 1);
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_INT_MODULO:
#else
        case OP_INT_MODULO:
#endif
        {
            int64_t ib = AS_INTEGER(REG(GET_C(instruction)));
            if (ib == 0) {
                runtimeError("Division by zero.");
                return INTERPRET_RUNTIME_ERROR;
            }
            int64_t ia = AS_INTEGER(REG(GET_B(instruction)));
            if (ia == INT64_MIN && ib == -1) {
                REG(GET_A(instruction)) = INTEGER_VAL(0);
            } else {
                STORE_INT(GET_A(instruction), ia % ib);
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_SQRT:
#else
        case OP_SQRT:
#endif
        {
            Value v = REG(GET_B(instruction));
            if (IS_INTEGER(v)) {
                REG_SET(GET_A(instruction), NUMBER_VAL(sqrt((double)AS_INTEGER(v))));
            } else if (IS_NUMBER(v)) {
                REG_SET(GET_A(instruction), NUMBER_VAL(sqrt(AS_NUMBER(v))));
            } else {
                runtimeError("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_POP:
#else
        case OP_POP:
#endif
        {
            REG(GET_A(instruction)) = NIL_VAL;
#ifdef __GNUC__
            DISPATCH();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_CALL:
#else
        case OP_CALL:
#endif
        {
             int reg = GET_A(instruction);
             int argCount = GET_B(instruction);
             Value callee = REG(reg);
             ObjClosure* closure = NULL;

             if (IS_CLOSURE(callee)) {
                 closure = AS_CLOSURE(callee);
             } else if (IS_FUNCTION(callee)) {
                 ObjFunction* function = AS_FUNCTION(callee);
                 closure = newClosure(function);
                  REG_SET(reg, OBJ_VAL(closure));

                 for (int i = 0; i < function->upvalueCount; i++) {
                     closure->upvalues[i] = NULL;
                 }
             } else if (IS_NATIVE(callee)) {
                 ObjNative* native = AS_NATIVE(callee);
                 if (native->arity >= 0 && argCount != native->arity) {
                     runtimeError("Expected %d arguments but got %d.",
                                  native->arity, argCount);
                     return INTERPRET_RUNTIME_ERROR;
                 }
                 Value result;
                 native->function(argCount, &REG(reg + 1), &result);
                 REG_SET(reg, result);
#ifdef __GNUC__
                 DISPATCH_POLL();
#else
                 break;
#endif
             } else if (IS_TABLE(callee)) {
                 ObjTable* table = AS_TABLE(callee);
                  if (table->metatable != NULL) {
                      ObjTable* mt = table->metatable;
                      Value callValue;
                      if (mt->metaGen == mt->writeGen && !IS_NIL(mt->cachedCall)) {
                          callValue = mt->cachedCall;
                      } else {
                          if (tableGet(&mt->fields, OBJ_VAL((Obj*)vm.mmCall), &callValue)) {
                              mt->cachedCall = callValue;
                              mt->metaGen = mt->writeGen;
                          } else {
                              callValue = NIL_VAL;
                          }
                      }
                      if (IS_CLOSURE(callValue)) {
                          closure = AS_CLOSURE(callValue);
                      } else if (IS_FUNCTION(callValue)) {
                          ObjFunction* function = AS_FUNCTION(callValue);
                          closure = newClosure(function);
                          for (int i = 0; i < function->upvalueCount; i++) {
                              closure->upvalues[i] = NULL;
                          }
                      } else {
                          runtimeError("Attempt to call a table without __call metamethod.");
                          return INTERPRET_RUNTIME_ERROR;
                      }
                 } else {
                     runtimeError("Attempt to call a table without metatable.");
                     return INTERPRET_RUNTIME_ERROR;
                 }
             } else {
                 runtimeError("Can only call functions.");
                 return INTERPRET_RUNTIME_ERROR;
             }

             if (argCount != closure->function->arity) {
                 runtimeError("Expected %d arguments but got %d.",
                              closure->function->arity, argCount);
                 return INTERPRET_RUNTIME_ERROR;
             }

             if (vm.frameCount >= FRAMES_MAX) {
                 runtimeError("Stack overflow.");
                 return INTERPRET_RUNTIME_ERROR;
             }

             CallFrame *nextFrame = &vm.frames[vm.frameCount];
             nextFrame->closure = closure;
             nextFrame->ip = closure->function->chunk.code;

             nextFrame->slots = &REG(reg);

             vm.frameCount++;
             RELOAD_FRAME();
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifdef __GNUC__
        OP_CLOSURE:
#else
        case OP_CLOSURE:
#endif
        {
            ObjFunction* function = AS_FUNCTION(READ_CONSTANT(instruction));
            ObjClosure* closure = newClosure(function);
            REG(GET_A(instruction)) = OBJ_VAL(closure);

            for (int i = 0; i < function->upvalueCount; i++) {
                uint8_t flags = READ_INST();
                uint8_t isLocal = flags & 1;
                uint8_t readonly = (flags >> 1) & 1;
                uint8_t index = READ_INST();
                if (readonly) {
                    if (isLocal) {
                        closure->readonlyValues[i] = REG(index);
                    } else {
                        closure->readonlyValues[i] = FRAME.closure->readonlyValues[index];
                    }
                } else {
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(&REG(index));
                    } else {
                        closure->upvalues[i] = FRAME.closure->upvalues[index];
                    }
                }
            }
#ifdef __GNUC__
            DISPATCH_POLL();
#else
            break;
#endif
        }

#ifndef __GNUC__
        }
    }
#endif
#ifdef __GNUC__
    }
#endif

#undef READ_INST
#undef RELOAD_FRAME
#undef READ_CONSTANT
#undef REG
#undef FRAME
#undef BINARY_OP
}

InterpretResult interpret(const char *source)
{
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    ObjClosure* closure = newClosure(function);

    vm.frameCount = 1;
    vm.frames[0].closure = closure;
    vm.frames[0].slots = vm.stack;
    vm.frames[0].ip = closure->function->chunk.code;

    return run(0);
}
