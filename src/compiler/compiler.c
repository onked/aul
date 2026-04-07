#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"
#include "compiler.h"
#include "chunk.h"

// compiler state - globals because it's simpler than threading pointers everywhere
Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;
int nextFreeRegister = 0;

void emitABC(OpCode op, int a, int b, int c) {
    writeChunk(compilingChunk, CREATE_ABC(op, a, b, c), parser.previous.line);
}

void emitABx(OpCode op, int a, int bx) {
    writeChunk(compilingChunk, CREATE_ABx(op, a, bx), parser.previous.line);
}

uint16_t makeConstant(Value value) {
    int constant = addConstant(compilingChunk, value);
    if (constant > 65535) {
        errorAt(&parser.previous, "Too many constants in one chunk.");
        return 0;
    }
    return (uint16_t)constant;
}

int allocateRegister() {
    if (nextFreeRegister >= 250) {
        errorAt(&parser.previous, "Too many temporary registers in expression.");
        return 0;
    }
    return nextFreeRegister++;
}

int emitJump(OpCode op) {
    emitABx(op, 0, 0xFFFF);
    return compilingChunk->count - 1;
}

void patchJump(int jumpPlaceholderOffset) {
    // -1 because ip already advanced past this instruction
    int jump = compilingChunk->count - jumpPlaceholderOffset - 1;
    if (jump > 65535) {
        errorAt(&parser.previous, "Too much code to jump over.");
    }
    
    uint32_t instruction = compilingChunk->code[jumpPlaceholderOffset];
    OpCode op = GET_OP(instruction);
    int a = GET_A(instruction);
    compilingChunk->code[jumpPlaceholderOffset] = CREATE_ABx(op, a, jump);
}

// add an upvalue, deduplicating if it already exists
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        CompilerUpvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == 250) {
        errorAt(&parser.previous, "Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    compiler->function->upvalues[upvalueCount].isLocal = isLocal;
    compiler->function->upvalues[upvalueCount].index = index;

    return compiler->function->upvalueCount++;
}

// walk up the compiler chain looking for a variable
int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocalInCompiler(compiler->enclosing, name);
    if (local != -1) {
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

void initCompiler(Compiler* compiler, Compiler* enclosing) {
    compiler->enclosing = enclosing;
    compiler->function = newFunction();
    compiler->localCount = 0;
    compiler->maxRegister = 0;
    compiler->upvalueCount = 0;
    compiler->scopeDepth = 0;
    compiler->currentLoop = NULL;
    
    current = compiler;
    compilingChunk = &current->function->chunk;

    // slot 0 reserved for the function itself
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
    local->reg = 0;
    
    nextFreeRegister = current->localCount;
}

ObjFunction* endCompiler() {
    emitABC(OP_RETURN, 0, 0, 0);
    ObjFunction* function = current->function;
    current = current->enclosing;
    
    if (current != NULL) {
        compilingChunk = &current->function->chunk;
        nextFreeRegister = current->maxRegister + 1;
    }
    return function;
}

void addLocal(Token name, int reg) {
    if (current->localCount >= 250) {
        errorAt(&name, "Too many local variables in scope.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = current->scopeDepth;
    local->reg = reg;
    
    if (reg > current->maxRegister) {
        current->maxRegister = reg;
    }
    
    if (nextFreeRegister <= reg) {
        nextFreeRegister = reg + 1;
    }
}

// compile source code into a function object, which can then be executed by the VM
ObjFunction* compile(const char* source) {
    initScanner(source);
    
    Compiler compiler;
    initCompiler(&compiler, NULL);

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    
    return parser.hadError ? NULL : function;
}
