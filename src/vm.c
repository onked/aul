#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "table.h"
#include "object.h"
#include "memory.h"

// uncomment this to see bytecode during execution
// #define DEBUG_TRACE_EXECUTION

VM vm;

// TODO: maybe make gc threshold configurable from cmdline?
// for now 128kb seems fine

static void resetStack()
{
    vm.frameCount = 0;
    // Base of the physical stack array
    vm.stackTop = vm.stack;
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
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 128;
    initTable(&vm.globals);
}

void freeVM()
{
    vm.grayStack = NULL;
    collectGarbage();
    freeTable(&vm.globals);
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
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static InterpretResult run()
{
    printf("VM STARTED\n");

#define FRAME (vm.frames[vm.frameCount - 1])
#define READ_INST() (*FRAME.ip++)
#define READ_CONSTANT(inst) (FRAME.closure->function->chunk.constants.values[GET_Bx(inst)])
#define REG(index) (FRAME.slots[index])

// binary op macro - checks types and applies the operator
#define BINARY_OP(op, metamethod_name)                               \
    do                                                               \
    {                                                                \
        uint32_t inst = FRAME.ip[-1];                                \
        Value b = REG(GET_C(inst));                                  \
        Value a = REG(GET_B(inst));                                  \
        if (IS_NUMBER(a) && IS_NUMBER(b)) {                          \
            REG(GET_A(inst)) = NUMBER_VAL(AS_NUMBER(a) op AS_NUMBER(b)); \
        } else if (IS_TABLE(a) && a.as.obj != NULL) {                \
            ObjTable* table = (ObjTable*)a.as.obj;                   \
            if (table->metatable != NULL) {                          \
                ObjString* mtStr = copyString(metamethod_name, strlen(metamethod_name)); \
                Value mtVal;                                         \
                if (tableGet(&table->metatable->fields, mtStr, &mtVal) && \
                    (IS_CLOSURE(mtVal) || IS_FUNCTION(mtVal))) {     \
                    /* Metamethod found but full call support is complex */ \
                                                                     \
                    runtimeError("Metamethod calls not fully supported yet."); \
                    return INTERPRET_RUNTIME_ERROR;                  \
                }                                                    \
            }                                                        \
            runtimeError("Operands must be numbers.");               \
            return INTERPRET_RUNTIME_ERROR;                          \
        } else {                                                     \
            runtimeError("Operands must be numbers.");               \
            return INTERPRET_RUNTIME_ERROR;                          \
        }                                                            \
    } while (false)

    for (;;)
    {
#ifdef DEBUG_TRACE_EXECUTION
        disassembleInstruction(&FRAME.closure->function->chunk, (int)(FRAME.ip - FRAME.closure->function->chunk.code));
#endif

        uint32_t instruction = READ_INST();
        switch (GET_OP(instruction))
        {

       case OP_CONSTANT:
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
            break;
        }

        case OP_ADD:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (IS_NUMBER(a) && IS_NUMBER(b)) {
                REG(GET_A(inst)) = NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b));
            } else if (IS_STRING(a) && IS_STRING(b)) {
                ObjString* strA = AS_STRING(a);
                ObjString* strB = AS_STRING(b);
                int length = strA->length + strB->length;
                char* chars = malloc(length + 1);
                memcpy(chars, strA->chars, strA->length);
                memcpy(chars + strA->length, strB->chars, strB->length);
                chars[length] = '\0';
                REG(GET_A(inst)) = OBJ_VAL(copyString(chars, length));
                free(chars);
            } else {
                runtimeError("Operands must be numbers or strings.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_SUBTRACT: BINARY_OP(-, "__sub"); break;
        case OP_MULTIPLY: BINARY_OP(*, "__mul"); break;
        case OP_DIVIDE:   BINARY_OP(/, "__div"); break;

        case OP_EQUAL:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            REG(GET_A(inst)) = BOOL_VAL(a.type == b.type && AS_NUMBER(a) == AS_NUMBER(b));
            break;
        }

        case OP_GREATER:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) > AS_NUMBER(b));
            break;
        }

        case OP_LESS:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) < AS_NUMBER(b));
            break;
        }

        case OP_GREATER_EQUAL:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) >= AS_NUMBER(b));
            break;
        }

        case OP_LESS_EQUAL:
        {
            uint32_t inst = FRAME.ip[-1];
            Value b = REG(GET_C(inst));
            Value a = REG(GET_B(inst));
            if (!IS_NUMBER(a) || !IS_NUMBER(b)) {
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(inst)) = BOOL_VAL(AS_NUMBER(a) <= AS_NUMBER(b));
            break;
        }

        case OP_NOT:
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
            break;
        }

        case OP_TABLE:
        {
            ObjTable* table = newTable();
            REG(GET_A(instruction)) = OBJ_VAL(table);
            break;
        }

        case OP_LENGTH:
        {
            uint8_t dest = GET_A(instruction);
            uint8_t src = GET_B(instruction);
            Value val = REG(src);
            if (IS_TABLE(val)) {
                ObjTable* table = AS_TABLE(val);
                if (table->metatable != NULL) {
                    ObjString* lenStr = copyString("__len", 5);
                    Value lenValue;
                    if (tableGet(&table->metatable->fields, lenStr, &lenValue)) {
                        if (IS_CLOSURE(lenValue) || IS_FUNCTION(lenValue)) {
                            // full call support is complex, but we would call __len with (table)
                        }
                    }
                }
                REG(dest) = NUMBER_VAL((double)table->arrayCapacity);
            } else {
                runtimeError("Operand must be a table.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_SET_METATABLE:
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
            break;
        }

        case OP_GET_METATABLE:
        {
            uint8_t dest = GET_A(instruction);
            uint8_t src = GET_B(instruction);
            Value val = REG(src);
            if (IS_TABLE(val)) {
                ObjTable* table = AS_TABLE(val);
                if (table->metatable != NULL) {
                    REG(dest) = OBJ_VAL(table->metatable);
                } else {
                    REG(dest) = NIL_VAL;
                }
            } else {
                runtimeError("Operand must be a table.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_GET_TABLE:
        {
            uint8_t dest = GET_A(instruction);
            uint8_t tableReg = GET_B(instruction);
            uint8_t keyReg = GET_C(instruction);
            Value tableVal = REG(tableReg);
            Value keyVal = REG(keyReg);
            
            if (!IS_TABLE(tableVal)) {
                runtimeError("Can only index into tables.");
                return INTERPRET_RUNTIME_ERROR;
            }
            
            ObjTable* table = AS_TABLE(tableVal);
            Value result;
            bool found = false;
            
            if (IS_NUMBER(keyVal)) {
                int index = (int)AS_NUMBER(keyVal);
                if (index >= 1 && index <= table->arrayCapacity) {
                    result = table->array[index - 1];
                    found = true;
                }
            } else if (IS_STRING(keyVal)) {
                found = tableGet(&table->fields, AS_STRING(keyVal), &result);
            } else {
                runtimeError("Table key must be a number or string.");
                return INTERPRET_RUNTIME_ERROR;
            }
            
            // If not found, check metatable for __index
            if (!found && table->metatable != NULL) {
                // Look for __index in metatable
                ObjString* indexStr = copyString("__index", 7);
                Value indexValue;
                if (tableGet(&table->metatable->fields, indexStr, &indexValue)) {
                    if (IS_TABLE(indexValue)) {
                        // __index is a table, look up key in it
                        ObjTable* mt = AS_TABLE(indexValue);
                        if (IS_NUMBER(keyVal)) {
                            int index = (int)AS_NUMBER(keyVal);
                            if (index >= 1 && index <= mt->arrayCapacity) {
                                result = mt->array[index - 1];
                                found = true;
                            }
                        } else if (IS_STRING(keyVal)) {
                            found = tableGet(&mt->fields, AS_STRING(keyVal), &result);
                        }
                    } else if (IS_CLOSURE(indexValue) || IS_FUNCTION(indexValue)) {
                        // full call support is complex, but we would call __index with (table, key)
                        result = indexValue;
                        found = true;
                    }
                }
            }
            
            if (!found) {
                result = NIL_VAL;
            }
            
            REG(dest) = result;
            break;
        }

        case OP_SET_TABLE:
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
            if (IS_NUMBER(keyVal)) {
                int index = (int)AS_NUMBER(keyVal);
                if (index >= 1 && index <= table->arrayCapacity && !IS_NIL(table->array[index - 1])) {
                    keyExists = true;
                }
            } else if (IS_STRING(keyVal)) {
                Value dummy;
                keyExists = tableGet(&table->fields, AS_STRING(keyVal), &dummy);
            }
            
            // If key doesn't exist, check __newindex metamethod
            if (!keyExists && table->metatable != NULL) {
                ObjString* newIndexStr = copyString("__newindex", 10);
                Value newIndexValue;
                if (tableGet(&table->metatable->fields, newIndexStr, &newIndexValue)) {
                    if (IS_TABLE(newIndexValue)) {
                        // __newindex is a table, set key there
                        ObjTable* mt = AS_TABLE(newIndexValue);
                        if (IS_NUMBER(keyVal)) {
                            int index = (int)AS_NUMBER(keyVal);
                            if (index >= 1 && index <= mt->arrayCapacity) {
                                mt->array[index - 1] = value;
                            }
                        } else if (IS_STRING(keyVal)) {
                            tableSet(&mt->fields, AS_STRING(keyVal), value);
                        }
                        break;
                    } else if (IS_CLOSURE(newIndexValue) || IS_FUNCTION(newIndexValue)) {
                        // full call support is complex, but we would call __newindex with (table, key, value)
                    }
                }
            }
            
            if (IS_NUMBER(keyVal)) {
                int index = (int)AS_NUMBER(keyVal);
                if (index < 1) {
                    runtimeError("Table index must be >= 1.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (index > table->arrayCapacity) {
                    int oldCapacity = table->arrayCapacity;
                    table->arrayCapacity = index;
                    table->array = realloc(table->array, sizeof(Value) * index);
                    for (int i = oldCapacity; i < index; i++) {
                        table->array[i] = NIL_VAL;
                    }
                }
                table->array[index - 1] = value;
            } else if (IS_STRING(keyVal)) {
                tableSet(&table->fields, AS_STRING(keyVal), value);
            } else {
                runtimeError("Table key must be a number or string.");
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_BREAK:
        {
            uint16_t offset = GET_Bx(instruction);
            FRAME.ip += offset;
            break;
        }

        case OP_CONTINUE:
        {
            int16_t offset = (int16_t)GET_Bx(instruction);
            FRAME.ip += offset;
            break;
        }

        case OP_NEGATE:
        {
            Value val = REG(GET_B(instruction));
            if (!IS_NUMBER(val))
            {
                runtimeError("Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(instruction)) = NUMBER_VAL(-AS_NUMBER(val));
            break;
        }

        case OP_PRINT:
        {
            printValue(REG(GET_A(instruction)));
            printf("\n");
            fflush(stdout);
            break;
        }

        case OP_DEFINE_GLOBAL:
        {
            ObjString *name = AS_STRING(READ_CONSTANT(instruction));
            tableSet(&vm.globals, name, REG(GET_A(instruction)));
            break;
        }

        case OP_GET_GLOBAL:
        {
            ObjString *name = AS_STRING(READ_CONSTANT(instruction));
            Value value;
            if (!tableGet(&vm.globals, name, &value))
            {
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            REG(GET_A(instruction)) = value;
            break;
        }

        case OP_SET_GLOBAL:
        {
            ObjString *name = AS_STRING(READ_CONSTANT(instruction));
            if (tableSet(&vm.globals, name, REG(GET_A(instruction))))
            {
                tableDelete(&vm.globals, name);
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_GET_UPVALUE:
        {
            uint8_t reg = GET_A(instruction);
            uint8_t slot = GET_B(instruction);
            REG(reg) = *FRAME.closure->upvalues[slot]->location;
            break;
        }

        case OP_SET_UPVALUE:
        {
            int slot = GET_A(instruction);
            *FRAME.closure->upvalues[slot]->location = REG(GET_B(instruction));
            break;
        }

        case OP_RETURN:
        {
            Value result = REG(GET_A(instruction));

            closeUpvalues(FRAME.slots);

            FRAME.slots[0] = result;

            vm.frameCount--;
            if (vm.frameCount == 0)
            {
                return INTERPRET_OK;
            }
            break;
        }

        case OP_MOVE:
        {
            uint8_t dest = GET_A(instruction);
            uint8_t src = GET_B(instruction);
            REG(dest) = REG(src);
            break;
        }

        case OP_TRUE:
        {
            REG(GET_A(instruction)) = BOOL_VAL(true);
            break;
        }

        case OP_FALSE:
        {
            REG(GET_A(instruction)) = BOOL_VAL(false);
            break;
        }

        case OP_NIL:
        {
            REG(GET_A(instruction)) = NIL_VAL;
            break;
        }

        case OP_JUMP:
        {
            int16_t offset = (int16_t)GET_Bx(instruction);
            FRAME.ip += offset;
            break;
        }

        case OP_JUMP_IF_FALSE:
        {
            uint8_t reg = GET_A(instruction);
            uint16_t offset = GET_Bx(instruction);
            Value val = REG(reg);
            bool isFalse = IS_BOOL(val) ? !AS_BOOL(val) : IS_NIL(val);
            if (isFalse) {
                FRAME.ip += offset;
            }
            break;
        }

        case OP_POP:
        {
            REG(GET_A(instruction)) = NIL_VAL;
            break;
        }

       case OP_CALL:
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
                 REG(reg) = OBJ_VAL(closure);

                 for (int i = 0; i < function->upvalueCount; i++) {
                     closure->upvalues[i] = NULL;
                 }
             } else if (IS_TABLE(callee)) {
                 // Check for __call metamethod
                 ObjTable* table = AS_TABLE(callee);
                 if (table->metatable != NULL) {
                     ObjString* callStr = copyString("__call", 6);
                     Value callValue;
                     if (tableGet(&table->metatable->fields, callStr, &callValue)) {
                         if (IS_CLOSURE(callValue)) {
                             closure = AS_CLOSURE(callValue);
                         } else if (IS_FUNCTION(callValue)) {
                             ObjFunction* function = AS_FUNCTION(callValue);
                             closure = newClosure(function);
                             for (int i = 0; i < function->upvalueCount; i++) {
                                 closure->upvalues[i] = NULL;
                             }
                         } else {
                             runtimeError("Metamethod __call must be a function.");
                             return INTERPRET_RUNTIME_ERROR;
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
             break;
         }

        case OP_CLOSURE:
        {
            ObjFunction* function = AS_FUNCTION(READ_CONSTANT(instruction));
            ObjClosure* closure = newClosure(function);
            REG(GET_A(instruction)) = OBJ_VAL(closure);

            for (int i = 0; i < function->upvalueCount; i++) {
                uint8_t isLocal = READ_INST();
                uint8_t index = READ_INST();
                if (isLocal) {
                    closure->upvalues[i] = captureUpvalue(&REG(index));
                } else {
                    closure->upvalues[i] = FRAME.closure->upvalues[index];
                }
            }
            break;
        }
         }
     }

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