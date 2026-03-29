#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "compiler.h"
#include "scanner.h"

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

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;

// Forward declarations
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

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

static void emitByte(uint8_t byte) {
    writeChunk(compilingChunk, byte);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(compilingChunk, value);
    if (constant > UINT8_MAX) {
        errorAt(&parser.previous, "Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void number() {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(value);
}

static void grouping() {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void unary() {
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

static void binary() {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        default: return;
    }
}

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        errorAt(&parser.previous, "Expect expression.");
        return;
    }

    prefixRule();

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void function() {
    consume(TOKEN_IDENTIFIER, "Expect function name.");
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    consume(TOKEN_RIGHT_PAREN, "Expect ')'.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

    // Instead of just advance(), we call declaration() to actually 
    // compile the code inside the function.
    while (parser.current.type != TOKEN_RIGHT_BRACE && parser.current.type != TOKEN_EOF) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after function body.");
}

static void varDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect variable name.");
    uint8_t nameIndex = makeConstant(0.0); 

    if (parser.current.type == TOKEN_EQUAL) {
        advance();
        expression();
    } else {
        emitConstant(0.0);
    }

    emitBytes(OP_DEFINE_GLOBAL, nameIndex);
}

static void statement() {
    expression();
    emitByte(OP_POP); 
}

static void returnStatement() {
    if (parser.current.type == TOKEN_SEMICOLON) {
        emitByte(OP_RETURN); // Returns nothing/void
    } else {
        expression(); // Puts the return value on the stack
        emitByte(OP_RETURN_VALUE); 
    }
}

static void declaration() {
    if (parser.current.type == TOKEN_LOC) {
        advance();
        varDeclaration();
    } else if (parser.current.type == TOKEN_FUNC) {
        advance();
        function();
    } else if (parser.current.type == TOKEN_RETURN) {
        advance();
        returnStatement();
    } else {
        statement();
    }

    if (parser.panicMode) {
        parser.panicMode = false;
        while (parser.current.type != TOKEN_EOF) {
            if (parser.previous.type == TOKEN_SEMICOLON) return;
            switch (parser.current.type) {
                case TOKEN_FUNC:
                case TOKEN_LOC:
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
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LOC]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUNC]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;
    parser.hadError = false;
    parser.panicMode = false;

    advance(); 
    while (parser.current.type != TOKEN_EOF) {
        declaration();
    }

    emitReturn();
    return !parser.hadError;
}