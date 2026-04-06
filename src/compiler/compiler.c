#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"
#include "compiler.h"

// Define the shared globals here
Parser parser;
Compiler* current = NULL; // Changed to a pointer to handle the linked list of compilers
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
    emitABx(op, 0, 0xFFFF); // Placeholder offset
    return compilingChunk->count - 1;
}

void patchJump(int jumpPlaceholderOffset) {
    // -1 to adjust for the fact that the IP will have already advanced
    int jump = compilingChunk->count - jumpPlaceholderOffset - 1;
    if (jump > 65535) {
        errorAt(&parser.previous, "Too much code to jump over.");
    }
    
    uint32_t instruction = compilingChunk->code[jumpPlaceholderOffset];
    OpCode op = GET_OP(instruction);
    int a = GET_A(instruction);
    compilingChunk->code[jumpPlaceholderOffset] = CREATE_ABx(op, a, jump);
}

// --- Scoping & Upvalues ---
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

// Recursively find a variable in outer scopes and "capture" it as an upvalue
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

// --- Statements & Declarations ---

static void initCompiler(Compiler* compiler, Compiler* enclosing) {
    compiler->enclosing = enclosing;
    compiler->function = newFunction();
    compiler->localCount = 0;
    compiler->upvalueCount = 0;
    compiler->scopeDepth = 0;
    
    current = compiler;
    compilingChunk = &current->function->chunk;

    // The first slot is always reserved for the function/closure itself
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
    local->reg = 0;
    
    // Reset the allocator for the new function's scope
    nextFreeRegister = current->localCount; 
}

static ObjFunction* endCompiler() {
    emitABC(OP_RETURN, 0, 0, 0);
    ObjFunction* function = current->function;
    current = current->enclosing;
    
    if (current != NULL) {
        compilingChunk = &current->function->chunk;
        
        // Restore the allocator to the outer function's state!
        nextFreeRegister = current->localCount; 
    }
    return function;
}

static void addLocal(Token name, int reg) {
    if (current->localCount >= 250) {
        errorAt(&name, "Too many local variables in scope.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = current->scopeDepth;
    local->reg = reg;
    
    local->reg = nextFreeRegister++;
}

static void globalDeclaration() {
    Token nameToken = parser.previous; 
    uint16_t nameIndex = makeConstant(OBJ_VAL(copyString(nameToken.start, nameToken.length)));
    int valReg;
    if (match(TOKEN_EQUAL)) {
        valReg = expression(); 
    } else {
        valReg = allocateRegister();
        emitABC(OP_NIL, valReg, 0, 0);
    }
    emitABx(OP_DEFINE_GLOBAL, valReg, nameIndex);
    match(TOKEN_SEMICOLON);
    nextFreeRegister = current->localCount; 
}

static void localDeclaration() {
    Token name = parser.previous;
    int reg;
    if (match(TOKEN_EQUAL)) {
        reg = expression(); 
    } else {
        reg = allocateRegister();
        emitABC(OP_NIL, reg, 0, 0);
    }
    addLocal(name, reg);
    match(TOKEN_SEMICOLON);
}

static void printStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'print'.");
    int reg = expression(); 
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    emitABC(OP_PRINT, reg, 0, 0);
    nextFreeRegister = current->localCount; 
}

static void returnStatement() {
    int reg;
    if (match(TOKEN_SEMICOLON)) {
        reg = allocateRegister();
        emitABC(OP_NIL, reg, 0, 0);
    } else {
        reg = expression(); 
        match(TOKEN_SEMICOLON);
    }
    emitABC(OP_RETURN, reg, 0, 0);
    nextFreeRegister = current->localCount; 
}

static void function() {
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    Token name = parser.previous;
    uint16_t nameIdx = makeConstant(OBJ_VAL(copyString(name.start, name.length)));

    // Create a new compiler for the function's own scope and chunk
    Compiler compiler;
    initCompiler(&compiler, current);
    current->function->name = copyString(name.start, name.length);

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 250) errorAt(&parser.current, "Too many parameters.");
            consume(TOKEN_IDENTIFIER, "Expect parameter name.");
            addLocal(parser.previous, current->function->arity); 
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')'.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after function body.");
    
    ObjFunction* functionObj = endCompiler();
    
    int addrReg = allocateRegister();
    emitABx(OP_CONSTANT, addrReg, makeConstant(OBJ_VAL(functionObj)));

    if (current->enclosing == NULL) { 
        emitABx(OP_DEFINE_GLOBAL, addrReg, nameIdx); 
    } else {
        addLocal(name, addrReg);
    }
    
    nextFreeRegister = current->localCount;
}

int call(int leftReg) {
    int callReg = allocateRegister();
    
    // it means leftReg was considered "free". We need to allocate AGAIN.
    if (callReg == leftReg) {
        callReg = allocateRegister();
    }

    emitABC(OP_MOVE, callReg, leftReg, 0);

    int argCount = 0;
    // Arguments must follow callReg (callReg + 1, callReg + 2, etc.)
    int nextArgSlot = callReg + 1; 

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            // Point the allocator to the specific slot for this argument
            nextFreeRegister = nextArgSlot;
            expression(); 
            nextArgSlot++;
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function arguments.");

    // The return value will overwrite callReg, but leftReg (our variable) stays safe!
    emitABC(OP_CALL, callReg, argCount, 0);

    nextFreeRegister = leftReg + 1; 
    return callReg;
}

void statement() {
    expression();
    match(TOKEN_SEMICOLON);
    nextFreeRegister = current->localCount; 
}

void declaration() {
    if (match(TOKEN_LOC)) {
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        localDeclaration();
    } else if (match(TOKEN_GLOBAL)) {
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        globalDeclaration();
    } else if (match(TOKEN_FUNC)) {
        function();
    } else if (match(TOKEN_PRINT)) {
        printStatement();
        match(TOKEN_SEMICOLON);
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else {
        statement();
    }
    if (parser.panicMode) synchronize();
}

// Compiles the given source code into the provided chunk. Returns true on success, false on error.
ObjFunction* compile(const char* source) {
    initScanner(source);
    
    Compiler compiler;
    initCompiler(&compiler, NULL); // NULL enclosing because this is 'main'

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    // Compile the actual source code until we hit the end of the file
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    
    // Check if we failed during compilation
    return parser.hadError ? NULL : function;
}