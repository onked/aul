#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"
#include "compiler.h"
#include "chunk.h"
#include "scanner.h"

static void ifStatement(void);
static void whileStatement(void);
static void forStatement(void);
static void breakStatement(void);
static void continueStatement(void);

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
}

static void function() {
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    Token name = parser.previous;
    uint16_t nameIdx = makeConstant(OBJ_VAL(copyString(name.start, name.length)));

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
    
    int closureReg = allocateRegister();
    emitABx(OP_CLOSURE, closureReg, makeConstant(OBJ_VAL(functionObj)));
    
    for (int i = 0; i < functionObj->upvalueCount; i++) {
        writeChunk(compilingChunk, functionObj->upvalues[i].isLocal ? 1 : 0, parser.previous.line);
        writeChunk(compilingChunk, functionObj->upvalues[i].index, parser.previous.line);
    }

    if (current->enclosing == NULL) {
        emitABx(OP_DEFINE_GLOBAL, closureReg, nameIdx);
    } else {
        addLocal(name, closureReg);
    }
}

int functionExpr(bool canAssign) {
    (void)canAssign;
    Compiler compiler;
    initCompiler(&compiler, current);
    current->function->name = NULL;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'func'.");
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
    
    int closureReg = allocateRegister();
    emitABx(OP_CLOSURE, closureReg, makeConstant(OBJ_VAL(functionObj)));
    
    for (int i = 0; i < functionObj->upvalueCount; i++) {
        writeChunk(compilingChunk, functionObj->upvalues[i].isLocal ? 1 : 0, parser.previous.line);
        writeChunk(compilingChunk, functionObj->upvalues[i].index, parser.previous.line);
    }

    return closureReg;
}

int call(int leftReg) {
    int callReg = allocateRegister();
    
    // it's possible the function being called is in the same register as the variable we're assigning to, so we need to move it to a safe place first
    if (callReg == leftReg) {
        callReg = allocateRegister();
    }

    emitABC(OP_MOVE, callReg, leftReg, 0);

    int argCount = 0;
    // arguments start at the next register after the function
    int nextArgSlot = callReg + 1; 

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            nextFreeRegister = nextArgSlot;
            expression(); 
            nextArgSlot++;
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function arguments.");

    // emit the call instruction with the function register and argument count
    emitABC(OP_CALL, callReg, argCount, 0);

    nextFreeRegister = leftReg + 1; 
    return callReg;
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    int conditionReg = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before if body.");
    
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    
    uint32_t inst = compilingChunk->code[thenJump];
    compilingChunk->code[thenJump] = CREATE_ABx(OP_JUMP_IF_FALSE, conditionReg, GET_Bx(inst));
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after if body.");
    
    if (match(TOKEN_ELSE)) {
        int elseJump = emitJump(OP_JUMP);
        
        patchJump(thenJump);

        if (check(TOKEN_IF)) {
            advance();
            ifStatement();
        } else {
            consume(TOKEN_LEFT_BRACE, "Expect '{' before else body.");
            while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
                declaration();
            }
            consume(TOKEN_RIGHT_BRACE, "Expect '}' after else body.");
        }
        
        patchJump(elseJump);
    } else {
        patchJump(thenJump);
    }
    
}

static void whileStatement() {
    int loopStart = compilingChunk->count;
    
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    int conditionReg = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before while body.");
    
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    
    uint32_t inst = compilingChunk->code[exitJump];
    compilingChunk->code[exitJump] = CREATE_ABx(OP_JUMP_IF_FALSE, conditionReg, GET_Bx(inst));
    
    Loop loop;
    loop.continueOffset = loopStart;
    loop.breakJump = exitJump;
    loop.enclosing = current->currentLoop;
    current->currentLoop = &loop;
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after while body.");
    
    int backOffset = loopStart - compilingChunk->count - 1;
    emitABx(OP_JUMP, 0, (uint16_t)backOffset);
    
    int breakJump = loop.breakJump;
    while (breakJump != -1 && breakJump != exitJump) {
        uint32_t inst = compilingChunk->code[breakJump];
        int nextBreak = GET_Bx(inst);
        int jumpToHere = compilingChunk->count - breakJump - 1;
        compilingChunk->code[breakJump] = CREATE_ABx(OP_JUMP, 0, (uint16_t)jumpToHere);
        breakJump = nextBreak;
    }
    patchJump(exitJump);
    
    current->currentLoop = loop.enclosing;
}

static void forStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    
    if (match(TOKEN_SEMICOLON)) {
         // no initializer
    } else if (match(TOKEN_LOC)) {
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        localDeclaration();
    } else {
        expression();
        match(TOKEN_SEMICOLON);
    }
    
    int loopStart = compilingChunk->count;
    int exitJump = -1;
    if (!check(TOKEN_SEMICOLON)) {
        int conditionReg = expression();
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        uint32_t inst = compilingChunk->code[exitJump];
        compilingChunk->code[exitJump] = CREATE_ABx(OP_JUMP_IF_FALSE, conditionReg, GET_Bx(inst));
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after for condition.");
    
    const char* incrStart = NULL;
    int incrLen = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        incrStart = parser.current.start;
        while (!check(TOKEN_RIGHT_PAREN) && !check(TOKEN_EOF)) {
            advance();
        }
        incrLen = (int)(parser.previous.start + parser.previous.length - incrStart);
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before for body.");
    
    Loop loop;
    loop.continueOffset = -1;
    loop.breakJump = exitJump;
    loop.enclosing = current->currentLoop;
    current->currentLoop = &loop;
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after for body.");
    
    if (incrStart != NULL) {
        loop.continueOffset = compilingChunk->count;
        Scanner savedScanner = scanner;
        Token savedPrev = parser.previous;
        Token savedCurr = parser.current;
        bool savedHadErr = parser.hadError;
        bool savedPanic = parser.panicMode;
        
        initScanner(incrStart);
        advance();
        while (!check(TOKEN_EOF) && !check(TOKEN_RIGHT_PAREN)) {
            const char* pos = parser.current.start;
            if ((pos - incrStart) >= incrLen) break;
            expression();
        }
        
        scanner = savedScanner;
        parser.previous = savedPrev;
        parser.current = savedCurr;
        parser.hadError = savedHadErr;
        parser.panicMode = savedPanic;
    } else {
        loop.continueOffset = loopStart;
    }
    
    int backOffset = loopStart - compilingChunk->count - 1;
    emitABx(OP_JUMP, 0, (uint16_t)backOffset);
    
    int breakJump = loop.breakJump;
    while (breakJump != -1 && breakJump != exitJump) {
        uint32_t inst = compilingChunk->code[breakJump];
        int nextBreak = GET_Bx(inst);
        int jumpToHere = compilingChunk->count - breakJump - 1;
        compilingChunk->code[breakJump] = CREATE_ABx(OP_JUMP, 0, (uint16_t)jumpToHere);
        breakJump = nextBreak;
    }
    
    if (exitJump != -1) {
        patchJump(exitJump);
    }
    
    current->currentLoop = loop.enclosing;
}

static void breakStatement() {
    if (current->currentLoop == NULL) {
        errorAt(&parser.previous, "Cannot use 'break' outside of a loop.");
        return;
    }
    int breakJump = emitJump(OP_JUMP);
    compilingChunk->code[breakJump] = CREATE_ABx(OP_JUMP, 0, current->currentLoop->breakJump);
    current->currentLoop->breakJump = breakJump;
    match(TOKEN_SEMICOLON);
}

static void continueStatement() {
    if (current->currentLoop == NULL) {
        errorAt(&parser.previous, "Cannot use 'continue' outside of a loop.");
        return;
    }
    int continueOffset = current->currentLoop->continueOffset;
    int backOffset = continueOffset - compilingChunk->count - 1;
    emitABx(OP_JUMP, 0, (uint16_t)backOffset);
    match(TOKEN_SEMICOLON);
}

void statement() {
    expression();
    match(TOKEN_SEMICOLON);
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
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else {
        statement();
    }
    if (parser.panicMode) synchronize();
}
