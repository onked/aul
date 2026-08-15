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
static void tryStatement(void);
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

static void localDeclaration(Token* names, int nameCount) {
    if (match(TOKEN_EQUAL)) {
        int base = allocateRegister();
        int count = 0;
        nextFreeRegister = base;
        int reg = expression();
        if (reg != base) {
            emitABC(OP_MOVE, base, reg, 0);
        }
        count = 1;
        bool isCall = (reg == lastCallReg);

        while (match(TOKEN_COMMA)) {
            if (count >= 250) {
                errorAt(&parser.current, "Too many values in declaration.");
                break;
            }
            nextFreeRegister = base + count;
            int vreg = expression();
            if (vreg != base + count) {
                emitABC(OP_MOVE, base + count, vreg, 0);
            }
            if (base + count > current->maxRegister) {
                current->maxRegister = base + count;
            }
            count++;
        }
        if (count > 1) isCall = false;

        for (int i = 0; i < nameCount; i++) {
            if (i < count) {
                addLocal(names[i], base + i);
            } else {
                int destReg = allocateRegister();
                if (isCall) {
                    emitABC(OP_GET_RET, destReg, i, 0);
                } else {
                    emitABC(OP_NIL, destReg, 0, 0);
                }
                addLocal(names[i], destReg);
            }
        }
    } else {
        for (int i = 0; i < nameCount; i++) {
            int nilReg = allocateRegister();
            emitABC(OP_NIL, nilReg, 0, 0);
            addLocal(names[i], nilReg);
        }
    }
    match(TOKEN_SEMICOLON);
}

static void printStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'print'.");
    if (match(TOKEN_RIGHT_PAREN)) {
        emitABC(OP_PRINT, 0, 2, 0);
        return;
    }
    int firstReg = allocateRegister();
    int argCount = 0;
    nextFreeRegister = firstReg;
    int reg = expression();
    if (reg != firstReg) {
        emitABC(OP_MOVE, firstReg, reg, 0);
    }
    argCount++;
    while (match(TOKEN_COMMA)) {
        if (firstReg + argCount >= 250) {
            errorAt(&parser.current, "Too many arguments to print.");
            break;
        }
        nextFreeRegister = firstReg + argCount;
        int exprReg = expression();
        if (exprReg != firstReg + argCount) {
            emitABC(OP_MOVE, firstReg + argCount, exprReg, 0);
        }
        argCount++;
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    for (int i = 0; i < argCount; i++) {
        emitABC(OP_PRINT, firstReg + i, (i == argCount - 1) ? 0 : 1, 0);
    }
    nextFreeRegister = firstReg;
}

static void returnStatement() {
    int base = -1;
    int count = 0;
    int firstReg = -1;
    if (!match(TOKEN_SEMICOLON)) {
        base = allocateRegister();
        count = 1;
        nextFreeRegister = base;
        int reg = expression();
        firstReg = reg;
        if (reg != base) {
            emitABC(OP_MOVE, base, reg, 0);
        }
        while (match(TOKEN_COMMA)) {
            if (base + count >= 250) {
                errorAt(&parser.current, "Too many return values.");
                break;
            }
            nextFreeRegister = base + count;
            int reg = expression();
            if (reg != base + count) {
                emitABC(OP_MOVE, base + count, reg, 0);
            }
            if (base + count > current->maxRegister) {
                current->maxRegister = base + count;
            }
            count++;
        }
    }
    match(TOKEN_SEMICOLON);

    if (count == 0) {
        int nilReg = allocateRegister();
        emitABC(OP_NIL, nilReg, 0, 0);
        emitABC(OP_RETURN, nilReg, 0, 0);
    } else if (count == 1 && firstReg == lastVarargReg) {
        emitABC(OP_RETURN_VARARG, 0, 0, 0);
    } else if (count == 1) {
        emitABC(OP_RETURN, base, 0, 0);
    } else {
        emitABC(OP_RETURN_MULTI, base, count, 0);
    }
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
            if (match(TOKEN_ELLIPSIS)) {
                current->function->isVararg = true;
                break;
            }
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
        uint8_t flags = functionObj->upvalues[i].isLocal ? 1 : 0;
        if (functionObj->upvalues[i].readonly) flags |= 2;
        writeChunk(compilingChunk, flags, parser.previous.line);
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
            if (match(TOKEN_ELLIPSIS)) {
                current->function->isVararg = true;
                break;
            }
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
        uint8_t flags = functionObj->upvalues[i].isLocal ? 1 : 0;
        if (functionObj->upvalues[i].readonly) flags |= 2;
        writeChunk(compilingChunk, flags, parser.previous.line);
        writeChunk(compilingChunk, functionObj->upvalues[i].index, parser.previous.line);
    }

    return closureReg;
}

int call(int leftReg) {
    int callReg = allocateRegister();
    
    if (callReg == leftReg) {
        callReg = allocateRegister();
    }

    emitABC(OP_MOVE, callReg, leftReg, 0);

    int argCount = 0;
    int nextArgSlot = callReg + 1; 
    bool isVararg = false;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            if (match(TOKEN_ELLIPSIS)) {
                if (!current->function->isVararg) {
                    errorAt(&parser.previous, "Cannot use '...' outside a variadic function.");
                }
                isVararg = true;
                nextFreeRegister = nextArgSlot;
                int temp = allocateRegister();
                emitABC(OP_VARARG, temp, 0, 0);
                break;
            }
            nextFreeRegister = nextArgSlot;
            int exprReg = expression();
            if (exprReg != nextArgSlot) {
                emitABC(OP_MOVE, nextArgSlot, exprReg, 0);
            }
            nextArgSlot++;
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function arguments.");

    emitABC(OP_CALL, callReg, argCount, isVararg ? 1 : 0);

    lastCallReg = callReg;
    lastVarargReg = -1;

    int callMinFree = current->maxRegister + 1;
    if (leftReg + 1 > callMinFree) callMinFree = leftReg + 1;
    nextFreeRegister = callMinFree;
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

static void forInStatement(Token keyVar, Token valueVar, int varCount);

static void forStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    
    if (check(TOKEN_IDENTIFIER)) {
        Scanner savedScanner = scanner;
        Token savedPrev = parser.previous;
        Token savedCurr = parser.current;
        advance();
        Token firstVar = parser.previous;
        bool isForIn = false;
        Token secondVar = firstVar;
        int varCount = 1;
        if (match(TOKEN_IN)) {
            isForIn = true;
        } else if (match(TOKEN_COMMA)) {
            if (check(TOKEN_IDENTIFIER)) {
                advance();
                secondVar = parser.previous;
                varCount = 2;
            } else {
                errorAt(&parser.current, "Expect loop variable name.");
            }
            if (match(TOKEN_IN)) {
                isForIn = true;
            }
        }
        if (isForIn) {
            forInStatement(firstVar, secondVar, varCount);
            return;
        }
        scanner = savedScanner;
        parser.previous = savedPrev;
        parser.current = savedCurr;
    }
    
    if (match(TOKEN_SEMICOLON)) {
         // no initializer
    } else if (match(TOKEN_LOC)) {
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        Token name = parser.previous;
        localDeclaration(&name, 1);
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

static void forInStatement(Token keyVar, Token valueVar, int varCount) {
    int tableReg = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after iteration table.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before for body.");
    
    int tmpTableReg = allocateRegister();
    emitABC(OP_MOVE, tmpTableReg, tableReg, 0);
    int keyReg = allocateRegister();
    int valueReg = allocateRegister();
    int cursorReg = allocateRegister();
    emitABx(OP_CONSTANT, cursorReg, makeConstant(INTEGER_VAL(0)));
    
    addLocal(valueVar, valueReg);
    if (varCount == 2) {
        addLocal(keyVar, keyReg);
    }
    Token cursorName;
    cursorName.start = "";
    cursorName.length = 0;
    cursorName.line = parser.previous.line;
    addLocal(cursorName, cursorReg);
    
    int instOffset = compilingChunk->count;
    emitABx(OP_FOR_IN, tmpTableReg, 0);
    
    Loop loop;
    loop.continueOffset = instOffset;
    loop.breakJump = instOffset;
    loop.enclosing = current->currentLoop;
    current->currentLoop = &loop;
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after for body.");
    
    int backOffset = instOffset - compilingChunk->count - 1;
    emitABx(OP_JUMP, 0, (uint16_t)backOffset);
    
    int breakJump = loop.breakJump;
    while (breakJump != -1 && breakJump != instOffset) {
        uint32_t inst = compilingChunk->code[breakJump];
        int nextBreak = GET_Bx(inst);
        int jumpToHere = compilingChunk->count - breakJump - 1;
        compilingChunk->code[breakJump] = CREATE_ABx(OP_JUMP, 0, (uint16_t)jumpToHere);
        breakJump = nextBreak;
    }
    uint32_t inst = compilingChunk->code[instOffset];
    compilingChunk->code[instOffset] = CREATE_ABx(OP_FOR_IN, GET_A(inst),
        (uint16_t)(compilingChunk->count - instOffset - 1));
    
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

static void tryStatement() {
    consume(TOKEN_LEFT_BRACE, "Expect '{' before try body.");

    int tryPos = compilingChunk->count;
    emitABx(OP_TRY, 0, 0);

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after try body.");

    emitABC(OP_ENDTRY, 0, 0, 0);

    int skipJump = emitJump(OP_JUMP);

    if (!match(TOKEN_CATCH)) {
        errorAt(&parser.previous, "Expect 'catch' after try body.");
    }
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'catch'.");
    consume(TOKEN_IDENTIFIER, "Expect catch variable name.");
    Token catchVar = parser.previous;
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after catch variable.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before catch body.");

    int handlerPos = compilingChunk->count;

    int catchReg = allocateRegister();
    addLocal(catchVar, catchReg);

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after catch body.");

    patchJump(skipJump);

    int handlerOffset = handlerPos - tryPos - 1;
    if (handlerOffset > 65535) {
        errorAt(&parser.previous, "Too much code in try body.");
    }
    compilingChunk->code[tryPos] = CREATE_ABx(OP_TRY, catchReg, (uint16_t)handlerOffset);
}

void statement() {
    expression();
    match(TOKEN_SEMICOLON);
}

static void reuseRegisters() {
    nextFreeRegister = current->localMaxReg + 1;
}

void declaration() {
    if (match(TOKEN_LOC)) {
        Token names[250];
        int nameCount = 0;
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        names[nameCount++] = parser.previous;
        while (match(TOKEN_COMMA)) {
            if (nameCount >= 250) {
                errorAt(&parser.current, "Too many local variables in declaration.");
                break;
            }
            consume(TOKEN_IDENTIFIER, "Expect variable name.");
            names[nameCount++] = parser.previous;
        }
        localDeclaration(names, nameCount);
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
    } else if (match(TOKEN_TRY)) {
        tryStatement();
    } else {
        statement();
    }
    reuseRegisters();
    if (parser.panicMode) synchronize();
}
