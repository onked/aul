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
    // The stack pointer points to the base of our physical stack array
    vm.stackTop = vm.stack;
}

// Helper for runtime errors
static void runtimeError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // Get IP from the current active frame
    CallFrame *frame = &vm.frames[vm.frameCount - 1];
    size_t instruction = frame->ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);

    resetStack();
}

void initVM()
{
    resetStack();
    initTable(&vm.globals);
}

void freeVM()
{
    freeTable(&vm.globals);
}

static InterpretResult run()
{
    printf("VM STARTED\n");

// Access the top-most call frame
#define FRAME (vm.frames[vm.frameCount - 1])

// Read the current 32-bit instruction from the current frame's IP
#define READ_INST() (*FRAME.ip++)

// Helper to get a constant using the Bx index from the instruction
#define READ_CONSTANT(inst) (vm.chunk->constants.values[GET_Bx(inst)])

// REGISTER WINDOWING: REG(index) is relative to the current frame's stack slots
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
        // Disassemble the next instruction based on the current frame's IP
        disassembleInstruction(vm.chunk, (int)(FRAME.ip - vm.chunk->code));
#endif

        uint32_t instruction = READ_INST();
        switch (GET_OP(instruction))
        {

        case OP_CONSTANT:
        {
            REG(GET_A(instruction)) = READ_CONSTANT(instruction);
            break;
        }

        case OP_ADD:
            BINARY_OP(+);
            break;
        case OP_SUBTRACT:
            BINARY_OP(-);
            break;
        case OP_MULTIPLY:
            BINARY_OP(*);
            break;
        case OP_DIVIDE:
            BINARY_OP(/);
            break;

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

        case OP_RETURN:
        {
            Value result = REG(GET_A(instruction)); 
            vm.frameCount--;

            if (vm.frameCount == 0)
            {
                return INTERPRET_OK;
            }

            // Restore the stack top to the caller and pass the result
            // The result goes into the register slot that previously held the function
            vm.frames[vm.frameCount - 1].slots[0] = result; 
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
            Value funcAddr = REG(reg);

            if (!IS_NUMBER(funcAddr))
            {
                runtimeError("Can only call functions (address expected).");
                return INTERPRET_RUNTIME_ERROR;
            }

            if (vm.frameCount >= FRAMES_MAX)
            {
                runtimeError("Stack overflow.");
                return INTERPRET_RUNTIME_ERROR;
            }

            Value* nextSlots = &REG(reg + 1);

            CallFrame *nextFrame = &vm.frames[vm.frameCount++];
            
            nextFrame->slots = nextSlots;
            nextFrame->ip = &vm.chunk->code[(int)AS_NUMBER(funcAddr)];
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
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk))
    {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;

    // Push the initial 'main' frame
    vm.frameCount = 1;
    vm.frames[0].slots = vm.stack;
    vm.frames[0].ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}