#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"
#include "compiler.h"
#include "chunk.h"
#include "memory.h"
#include "optimizer.h"

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
    int reg = nextFreeRegister++;
    if (reg > current->maxRegister) {
        current->maxRegister = reg;
    }
    return reg;
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
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal, bool readonly) {
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
    compiler->upvalues[upvalueCount].readonly = readonly;
    compiler->function->upvalues[upvalueCount].isLocal = isLocal;
    compiler->function->upvalues[upvalueCount].index = index;
    compiler->function->upvalues[upvalueCount].readonly = readonly;

    return compiler->function->upvalueCount++;
}

// walk up the compiler chain looking for a variable
int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocalInCompiler(compiler->enclosing, name);
    if (local != -1) {
        bool readonly = true;
        for (int i = 0; i < compiler->enclosing->localCount; i++) {
            if ((int)compiler->enclosing->locals[i].reg == local) {
                readonly = !compiler->enclosing->locals[i].mutated;
                break;
            }
        }
        return addUpvalue(compiler, (uint8_t)local, true, readonly);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        bool readonly = compiler->enclosing->upvalues[upvalue].readonly;
        return addUpvalue(compiler, (uint8_t)upvalue, false, readonly);
    }

    return -1;
}

void initCompiler(Compiler* compiler, Compiler* enclosing) {
    compiler->enclosing = enclosing;
    compiler->function = newFunction();
    compiler->localCount = 0;
    compiler->maxRegister = 0;
    compiler->localMaxReg = 0;
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

static void patchReadonlyUpvalues(ObjFunction* func, int upvalueIndex) {
    for (int i = 0; i < func->chunk.count; i++) {
        uint32_t inst = func->chunk.code[i];
        if (GET_OP(inst) == OP_GET_READONLY_UPVALUE && GET_B(inst) == (uint8_t)upvalueIndex) {
            func->chunk.code[i] = CREATE_ABC(OP_GET_UPVALUE, GET_A(inst), upvalueIndex, 0);
        }
        if (GET_OP(inst) == OP_CLOSURE) {
            uint16_t bx = GET_Bx(inst);
            if (bx < func->chunk.constants.count) {
                Value childVal = func->chunk.constants.values[bx];
                if (IS_OBJ(childVal) && OBJ_TYPE(childVal) == OBJ_FUNCTION) {
                    ObjFunction* child = AS_FUNCTION(childVal);
                    int childMetaOffset = i + 1;
                    for (int cj = 0; cj < child->upvalueCount; cj++) {
                        int childByteOffset = childMetaOffset + cj * 2;
                        if (childByteOffset + 1 >= func->chunk.count) break;
                        uint8_t childFlags = func->chunk.code[childByteOffset];
                        if (!(childFlags & 2)) continue;
                        uint8_t childIsLocal = childFlags & 1;
                        uint8_t childIndex = func->chunk.code[childByteOffset + 1];
                        if (!childIsLocal && (int)childIndex == upvalueIndex) {
                            func->chunk.code[childByteOffset] = childFlags & ~2;
                            patchReadonlyUpvalues(child, cj);
                        }
                    }
                }
            }
        }
    }
}

static void fixupUpvalueMetadata() {
    for (int i = 0; i < compilingChunk->count; i++) {
        uint32_t inst = compilingChunk->code[i];
        if (GET_OP(inst) != OP_CLOSURE) continue;

        uint16_t bx = GET_Bx(inst);
        if (bx >= compilingChunk->constants.count) continue;
        Value val = compilingChunk->constants.values[bx];
        if (!IS_OBJ(val) || OBJ_TYPE(val) != OBJ_FUNCTION) continue;

        ObjFunction* func = AS_FUNCTION(val);
        int metaOffset = i + 1;

        for (int j = 0; j < func->upvalueCount; j++) {
            int byteOffset = metaOffset + j * 2;
            if (byteOffset + 1 >= compilingChunk->count) break;
            uint8_t flags = compilingChunk->code[byteOffset];
            if (!(flags & 2)) continue;
            uint8_t isLocal = flags & 1;
            if (!isLocal) continue;

            uint8_t index = compilingChunk->code[byteOffset + 1];
            bool mutated = false;
            for (int k = 0; k < current->localCount; k++) {
                if ((uint8_t)current->locals[k].reg == index) {
                    mutated = current->locals[k].mutated;
                    break;
                }
            }
            if (mutated) {
                compilingChunk->code[byteOffset] = flags & ~2;
                patchReadonlyUpvalues(func, j);
            }
        }
    }
}

ObjFunction* endCompiler() {
    emitABC(OP_RETURN, 0, 0, 0);
    fixupUpvalueMetadata();
    ObjFunction* function = current->function;
    optimizeChunk(&function->chunk);
    specializeTypes(&function->chunk);
    foldCompareJumps(&function->chunk);
    removeNops(&function->chunk);
    
    function->maxRegs = current->maxRegister + 1;
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
    local->mutated = false;
    
    if (reg > current->maxRegister) {
        current->maxRegister = reg;
    }
    if (reg > current->localMaxReg) {
        current->localMaxReg = reg;
    }
    
    if (nextFreeRegister <= reg) {
        nextFreeRegister = reg + 1;
    }
}

// compile source code into a function object, which can then be executed by the VM
ObjFunction* compile(const char* source) {
    setCompiling(true);
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
    
    setCompiling(false);
    return parser.hadError ? NULL : function;
}
