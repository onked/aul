#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "compiler.h"
#include "debug.h"
#include "vm.h"
#include "table.h"
#include "object.h"
#include "memory.h"

#define DEBUG_TRACE_EXECUTION

VM vm;

static void resetStack()
{
    vm.frameCount = 0;
    // Base of the physical stack array
    vm.stackTop = vm.stack;
    vm.openUpvalues = NULL;
}

// Spits out errors when the code does something stupid at runtime
static void runtimeError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Grab the IP from whichever frame was running when it blew up
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
    initTable(&vm.globals);
}

void freeVM()
{
    freeTable(&vm.globals);
    // Remember to add freeObjects() here later so we don't leak memory
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    return createdUpvalue;
}

static InterpretResult run()
{
    printf("VM STARTED\n");

// Just a shortcut to the frame at the top of the call stack
#define FRAME (vm.frames[vm.frameCount - 1])

// Grabs the next 32-bit instruction and bumps the instruction pointer
#define READ_INST() (*FRAME.ip++)

// Pulls a constant from the chunk using the Bx index
#define READ_CONSTANT(inst) (FRAME.closure->function->chunk.constants.values[GET_Bx(inst)])

// Register windowing: treats the stack like a set of registers for the current frame
#define REG(index) (FRAME.slots[index])

#define BINARY_OP(op)                                                \
    do                                                               \
    {                                                                \
        uint32_t inst = FRAME.ip[-1];                                \
        Value b = REG(GET_C(inst));                                  \
        Value a = REG(GET_B(inst));                                  \
        if (!IS_NUMBER(a) || !IS_NUMBER(b))                          \
        {                                                            \
            runtimeError("Operands must be numbers.");               \
            return INTERPRET_RUNTIME_ERROR;                          \
        }                                                            \
        REG(GET_A(inst)) = NUMBER_VAL(AS_NUMBER(a) op AS_NUMBER(b)); \
    } while (false)

    for (;;)
    {
#ifdef DEBUG_TRACE_EXECUTION
        // Show us what the VM is about to do
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
                        // Capture the local variable from the current frame's register bank
                        closure->upvalues[i] = captureUpvalue(&REG(index));
                    } else {
                        // Pass through an existing upvalue
                        closure->upvalues[i] = FRAME.closure->upvalues[index];
                    }
                }
            } else {
                REG(GET_A(instruction)) = constant;
            }
            break;
        }

        case OP_ADD:      BINARY_OP(+); break;
        case OP_SUBTRACT: BINARY_OP(-); break;
        case OP_MULTIPLY: BINARY_OP(*); break;
        case OP_DIVIDE:   BINARY_OP(/); break;

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
                // Delete if it wasn't actually there (keeps us from creating new globals with SET)
                tableDelete(&vm.globals, name);
                runtimeError("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }

        case OP_GET_UPVALUE:
        {
            uint8_t reg = READ_INST();
            uint8_t slot = READ_INST();
            // It's pulling from the closure's upvalue array
            REG(reg) = *vm.frames[vm.frameCount - 1].closure->upvalues[slot]->location;
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

            // Put the result in Slot 0 of the CURRENT frame.
            // Because of our sliding stack window, this perfectly overwrites
            // the function in the caller's register with the return value!
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
            uint16_t offset = GET_Bx(instruction);
            FRAME.ip += offset;
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

    // Wrap the top-level script function in a closure
    ObjClosure* closure = newClosure(function);
    
    // Kick off the first frame
    vm.frameCount = 1;
    vm.frames[0].closure = closure;
    vm.frames[0].slots = vm.stack;
    vm.frames[0].ip = closure->function->chunk.code;

    return run();
}