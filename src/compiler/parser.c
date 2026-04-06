#include <stdio.h>
#include <stdarg.h>

#include "compiler_shared.h"
#include "scanner.h"

// Error handling
void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // TOKEN_ERROR todo
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// Updates the parser to the next valid token from the scanner.
void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        // The scanner puts the error message in the start pointer
        errorAt(&parser.current, parser.current.start);
    }
}

// Validates that the current token matches the expected type.
void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAt(&parser.current, message);
}

// Non-consuming check: returns true if current token is of type 'type'.
bool check(TokenType type) {
    return parser.current.type == type;
}

// Consuming check: if the current token matches, it advances and returns true.
bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

/**
 * Error Recovery: Skips tokens until we reach a statement boundary 
 * (like a semicolon or a keyword). This prevents one syntax error 
 * from ruining the compilation of the rest of the file.
 */
void synchronize() {
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

            default:
                ;
        }

        advance();
    }
}