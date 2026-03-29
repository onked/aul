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
    vm.stackTop = vm.registers;
}

// Helper for runtime errors (like adding a string to a number)
static void runtimeError(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
    
    resetStack();
}

void initVM()
{
    resetStack();
    // Initialize our global variable storage
    initTable(&vm.globals);
}

void freeVM()
{
    // Clean up globals when the VM shuts down
    freeTable(&vm.globals);
}

static InterpretResult run() {
    printf("VM STARTED\n");

// Read the current 32-bit instruction and move to the next
#define READ_INST() (*vm.ip++)

// Helper to get a constant using the Bx index from the instruction
#define READ_CONSTANT(inst) (vm.chunk->constants.values[GET_Bx(inst)])

// Register access helper
#define REG(index) (vm.registers[index])

#define BINARY_OP(op) \
    do { \
        uint32_t inst = vm.ip[-1]; \
        Value b = REG(GET_C(inst)); \
        Value a = REG(GET_B(inst)); \
        if (!IS_NUMBER(a) || !IS_NUMBER(b)) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        REG(GET_A(inst)) = NUMBER_VAL(AS_NUMBER(a) op AS_NUMBER(b)); \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

        uint32_t instruction = READ_INST();
        switch (GET_OP(instruction)) {
            
            case OP_CONSTANT: {
                // Load constant into register A
                REG(GET_A(instruction)) = READ_CONSTANT(instruction);
                break;
            }

            case OP_ADD:      BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE:   BINARY_OP(/); break;

            case OP_NEGATE: {
                Value val = REG(GET_B(instruction));
                if (!IS_NUMBER(val)) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                REG(GET_A(instruction)) = NUMBER_VAL(-AS_NUMBER(val));
                break;
            }

            case OP_PRINT: {
                printValue(REG(GET_A(instruction)));
                printf("\n");
                break;
            }

            case OP_DEFINE_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT(instruction));
                tableSet(&vm.globals, name, REG(GET_A(instruction)));
                break;
            }

            case OP_GET_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT(instruction));
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                REG(GET_A(instruction)) = value;
                break;
            }

            case OP_SET_GLOBAL: {
                ObjString* name = AS_STRING(READ_CONSTANT(instruction));
                if (tableSet(&vm.globals, name, REG(GET_A(instruction)))) {
                    tableDelete(&vm.globals, name); 
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_RETURN: {
                return INTERPRET_OK;
            }
        }
    }

#undef READ_INST
#undef READ_CONSTANT
#undef REG
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
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}