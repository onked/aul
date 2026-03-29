#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "value.h"
#include "chunk.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef int (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    int (*infix)(int leftReg); 
    Precedence precedence;
} ParseRule;

// --- Local Variable Tracking ---
typedef struct {
    Token name;
    int depth;
    int reg;
} Local;

typedef struct {
    Local locals[250];
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler current;
Chunk* compilingChunk;
int nextFreeRegister = 0;

// Forward declarations
static int expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static int parsePrecedence(Precedence precedence);
static void errorAt(Token* token, const char* message);

static void initCompiler() {
    current.localCount = 0;
    current.scopeDepth = 0;
}

static int allocateRegister() {
    if (nextFreeRegister >= 250) {
        errorAt(&parser.previous, "Too many temporary registers in expression.");
        return 0;
    }
    return nextFreeRegister++;
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void advance() {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;
        errorAt(&parser.current, parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAt(&parser.current, message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitABC(OpCode op, int a, int b, int c) {
    writeChunk(compilingChunk, CREATE_ABC(op, a, b, c), parser.previous.line);
}

static void emitABx(OpCode op, int a, int bx) {
    writeChunk(compilingChunk, CREATE_ABx(op, a, bx), parser.previous.line);
}

static uint16_t makeConstant(Value value) {
    int constant = addConstant(compilingChunk, value);
    if (constant > 65535) {
        errorAt(&parser.previous, "Too many constants in one chunk.");
        return 0;
    }
    return (uint16_t)constant;
}

static int number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    int reg = allocateRegister();
    emitABx(OP_CONSTANT, reg, makeConstant(NUMBER_VAL(value)));
    return reg;
}

static int string(bool canAssign) {
    (void)canAssign;
    Value value = OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2));
    int reg = allocateRegister();
    emitABx(OP_CONSTANT, reg, makeConstant(value));
    return reg;
}

static int grouping(bool canAssign) {
    (void)canAssign;
    int reg = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return reg;
}

static int unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    int argReg = parsePrecedence(PREC_UNARY);
    
    // We can reuse the argument register for the result
    int destReg = argReg; 
    
    switch (operatorType) {
        case TOKEN_MINUS: emitABC(OP_NEGATE, destReg, argReg, 0); break;
        default: return 0;
    }
    return destReg;
}

static int binary(int leftReg) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    
    int rightReg = parsePrecedence((Precedence)(rule->precedence + 1));
    
    // The result of the operation can go back into leftReg.
    int destReg = leftReg; 
    
    switch (operatorType) {
        case TOKEN_PLUS:   emitABC(OP_ADD,       destReg, leftReg, rightReg); break;
        case TOKEN_MINUS:  emitABC(OP_SUBTRACT, destReg, leftReg, rightReg); break;
        case TOKEN_STAR:   emitABC(OP_MULTIPLY, destReg, leftReg, rightReg); break;
        case TOKEN_SLASH:  emitABC(OP_DIVIDE,   destReg, leftReg, rightReg); break;
        default: return 0;
    }

    nextFreeRegister--; 
    
    return destReg;
}

static int resolveLocal(Token* name) {
    for (int i = current.localCount - 1; i >= 0; i--) {
        Local* local = &current.locals[i];
        if (name->length == local->name.length &&
            memcmp(name->start, local->name.start, name->length) == 0) {
            return local->reg;
        }
    }
    return -1;
}

static int variable(bool canAssign) {
    Token name = parser.previous;
    int reg = resolveLocal(&name);

    if (reg != -1) {
        if (canAssign && match(TOKEN_EQUAL)) {
            int valReg = expression();
            emitABC(OP_MOVE, reg, valReg, 0);
            nextFreeRegister--;
        }
        return reg;
    } else {
        uint16_t arg = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
        if (canAssign && match(TOKEN_EQUAL)) {
            int valReg = expression();
            emitABx(OP_SET_GLOBAL, valReg, arg);
            return valReg;
        } else {
            int destReg = allocateRegister();
            emitABx(OP_GET_GLOBAL, destReg, arg);
            return destReg;
        }
    }
}

static int parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        errorAt(&parser.previous, "Expect expression.");
        return 0;
    }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    int leftReg = prefixRule(canAssign);
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        int (*infixRule)(int) = (int (*)(int))getRule(parser.previous.type)->infix;
        if (infixRule != NULL) leftReg = infixRule(leftReg);
    }
    return leftReg;
}

static int expression() {
    return parsePrecedence(PREC_ASSIGNMENT);
}

static void addLocal(Token name, int reg) {
    if (current.localCount >= 250) {
        errorAt(&name, "Too many local variables in scope.");
        return;
    }
    Local* local = &current.locals[current.localCount++];
    local->name = name;
    local->depth = current.scopeDepth;
    local->reg = reg;
    
    nextFreeRegister = current.localCount;
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
    nextFreeRegister = current.localCount; 
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
    
    nextFreeRegister = current.localCount; 
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
    nextFreeRegister = current.localCount; 
}

static void statement() {
    expression();
    match(TOKEN_SEMICOLON);
    nextFreeRegister = current.localCount; 
}

static int emitJump(OpCode op) {
    emitABx(op, 0, 0xFFFF); // Placeholder
    return compilingChunk->count - 1;
}

static void patchJump(int jumpPlaceholderOffset) {
    int jump = compilingChunk->count - jumpPlaceholderOffset - 1;
    if (jump > 65535) {
        errorAt(&parser.previous, "Too much code to jump over.");
    }
    uint32_t instruction = compilingChunk->code[jumpPlaceholderOffset];
    OpCode op = GET_OP(instruction);
    compilingChunk->code[jumpPlaceholderOffset] = CREATE_ABx(op, GET_A(instruction), jump);
}

static void function() {
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    Token name = parser.previous;

    // Emit a jump that skips the body. 
    int skipJump = emitJump(OP_JUMP); 

    // Record the starting address (the next instruction)
    int functionAddress = compilingChunk->count;

    // Save state so we can restore it after compiling the function
    int previousLocalCount = current.localCount;
    int previousScopeDepth = current.scopeDepth;
    int previousNextFree = nextFreeRegister;

    current.localCount = 0;
    current.scopeDepth = 1; // Start at 1 for parameters/body
    nextFreeRegister = 0;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    
    // Parameter Parsing
    int arity = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            arity++;
            if (arity > 250) {
                errorAt(&parser.current, "Can't have more than 250 parameters.");
            }
            consume(TOKEN_IDENTIFIER, "Expect parameter name.");
            // Parameters are the first locals in the function frame
            addLocal(parser.previous, arity - 1); 
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')'.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after function body.");
    
    // Function needs a return at the end (defaults to nil in Register 0)
    emitABC(OP_RETURN, 0, 0, 0);

    // Restore the compiler state
    current.localCount = previousLocalCount;
    current.scopeDepth = previousScopeDepth;
    nextFreeRegister = previousNextFree;

    // Patch the jump so it skips exactly to here
    patchJump(skipJump);

    // FIX: Load the address into a register first
    int addrReg = allocateRegister();
    emitABx(OP_CONSTANT, addrReg, makeConstant(NUMBER_VAL(functionAddress)));

    // Store the function address in a Global
    uint16_t nameIdx = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
    emitABx(OP_DEFINE_GLOBAL, addrReg, nameIdx); 

    // Clean up temp register
    nextFreeRegister = current.localCount;
}

static int call(int leftReg) {
    int argCount = 0;
    
    // Parameters are evaluated into consecutive registers starting right after the function register
    nextFreeRegister = leftReg + 1;

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression(); 
            argCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after function arguments.");
    
    // Use 'B' to store the number of arguments (arity)
    emitABC(OP_CALL, leftReg, argCount, 0);
    
    // Reset nextFreeRegister to right after the call result
    nextFreeRegister = leftReg + 1;
    return leftReg;
}

static void synchronize() {
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_FUNC:
            case TOKEN_LOC:
            case TOKEN_GLOBAL:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default: ;
        }
        advance();
    }
}

static void declaration() {
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

static int literal(bool canAssign) {
    (void)canAssign;
    int reg = allocateRegister();
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitABC(OP_FALSE, reg, 0, 0); break;
        case TOKEN_NIL:   emitABC(OP_NIL,   reg, 0, 0); break;
        case TOKEN_TRUE:  emitABC(OP_TRUE,  reg, 0, 0); break;
        default: return 0; // Unreachable
    }
    return reg; 
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, (int(*)(int))call, PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    (int(*)(int))binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     (int(*)(int))binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     (int(*)(int))binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     (int(*)(int))binary, PREC_FACTOR},
    [TOKEN_BANG]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_GREATER]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LESS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LESS_EQUAL]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUNC]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LOC]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_GLOBAL]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    initCompiler();
    compilingChunk = chunk;
    parser.hadError = false;
    parser.panicMode = false;
    nextFreeRegister = 0;
    advance(); 
    while (parser.current.type != TOKEN_EOF) {
        declaration();
    }
    int r = allocateRegister();
    emitABC(OP_NIL, r, 0, 0);
    emitABC(OP_RETURN, r, 0, 0);
    return !parser.hadError;
}