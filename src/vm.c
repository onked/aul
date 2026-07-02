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
    for (int i = 0; i < IC_SIZE; i++) {
        vm.inlineCache[i].valid = false;
    }
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
    if (vm.gcPhase != GC_PHASE_IDLE && IS_OBJ(value)) {
        markObject(AS_OBJ(value));
    }
}

void gcStep(void)
{
    switch (vm.gcPhase) {
        case GC_PHASE_IDLE:
            return;

        case GC_PHASE_MARK_ROOTS:
#ifdef DEBUG_LOG_GC
            printf("-- gc begin (incremental)\n");
#endif
            markRoots();
            vm.gcPhase = GC_PHASE_MARK;
            break;
        case GC_PHASE_MARK:
        {
            size_t marksDone = 0;

            // rescan registers that may have been modified since the last step
            if (vm.frameCount > 0) {
                CallFrame* frame = &vm.frames[vm.frameCount - 1];
                int regCount = frame->closure->function->maxRegs;
                if (regCount == 0) regCount = 1;
                for (int r = 0; r < regCount; r++) {
                    markValue(frame->slots[r]);
                }
            }

            while (vm.grayCount > 0 && marksDone < vm.marksPerStep) {
                Obj* object = vm.grayStack[--vm.grayCount];
                blackenObject(object);
                marksDone++;
            }

            if (vm.grayCount == 0) {
                vm.gcPhase = GC_PHASE_SWEEP;
                vm.sweepObj = vm.objects;
            }
            break;
        }

        case GC_PHASE_SWEEP:
        {
            size_t swept = 0;
            Obj* previous = NULL;
            Obj* object = vm.sweepObj;

            while (object != NULL && swept < vm.marksPerStep) {
                if (object->marked) {
                    object->marked = false;
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

            if (object == NULL) {
                vm.gcPhase = GC_PHASE_IDLE;
                vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
                printf("-- gc end (incremental)\n");
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

static InterpretResult run()
{

#define FRAME (vm.frames[vm.frameCount - 1])
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

#define GC_WRITE_BARRIER(val) do { \
    if (vm.gcPhase != GC_PHASE_IDLE && IS_OBJ(val)) { \
        markObject(AS_OBJ(val)); \
    } \
} while (0)

#define REG_SET(r, val) do { \
    Value _v = (val); \
    GC_WRITE_BARRIER(_v); \
    FRAME.slots[(r)] = _v; \
} while (0)

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
        [OP_INT_JLT] = &&OP_INT_JLT,
        [OP_INT_JLE] = &&OP_INT_JLE,
        [OP_INT_JGT] = &&OP_INT_JGT,
        [OP_INT_JGE] = &&OP_INT_JGE,
        [OP_INT_JE] = &&OP_INT_JE,
        [OP_NOT_EQUAL] = &&OP_NOT_EQUAL,
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
                runtimeError("Operands must be numbers or strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
        }

        OP_DIVIDE:
        BINARY_OP(/);
        GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];

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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            if (IS_TABLE(val)) {
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
                table->metatable = AS_TABLE(mtVal);
            } else if (IS_NIL(mtVal)) {
                table->metatable = NULL;
            } else {
                runtimeError("Metatable must be a table or nil.");
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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

            // Inline cache check
            Chunk* chunk = &FRAME.closure->function->chunk;
            int instIdx = (int)(FRAME.ip - 1 - chunk->code);
            int icIdx = instIdx & (IC_SIZE - 1);
            InlineCache* ic = &vm.inlineCache[icIdx];
            if (ic->valid && ic->chunk == chunk && ic->instOffset == instIdx &&
                ic->table == tableVal && ic->key == keyVal)
            {
                if (ic->tableGen == AS_TABLE(tableVal)->writeGen) {
                    REG_SET(dest, ic->result);
#ifdef __GNUC__
                    GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
                    found = tableGet(&table->fields, keyVal, &result);
                }
            } else if (IS_NUMBER(keyVal)) {
                found = tableGet(&table->fields, keyVal, &result);
            } else if (IS_STRING(keyVal)) {
                found = tableGet(&table->fields, keyVal, &result);
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
                                found = tableGet(&idxTable->fields, keyVal, &result);
                            }
                        } else if (IS_NUMBER(keyVal)) {
                            found = tableGet(&idxTable->fields, keyVal, &result);
                        } else if (IS_STRING(keyVal)) {
                            found = tableGet(&idxTable->fields, keyVal, &result);
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
            ic->chunk = chunk;
            ic->instOffset = instIdx;
            ic->table = tableVal;
            ic->tableGen = table->writeGen;
            ic->key = keyVal;
            ic->result = result;
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
                        GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            Value value;
            if (!tableGet(&vm.globals, OBJ_VAL((Obj*)name), &value))
            {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(instruction)) = value;
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            if (tableSet(&vm.globals, OBJ_VAL((Obj*)name), REG(GET_A(instruction))))
            {
                tableDelete(&vm.globals, OBJ_VAL((Obj*)name));
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            if (vm.frameCount == 0)
            {
                return INTERPRET_OK;
            }
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
#ifdef __GNUC__
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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
            GC_STEP(); instruction = READ_INST(); goto *dispatch[GET_OP(instruction)];
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

    return run();
}
