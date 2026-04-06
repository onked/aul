#include "compiler_shared.h"

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, (int(*)(int))call, PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,    PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,    PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,    PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,    PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,    PREC_NONE},
    [TOKEN_MINUS]         = {unary,    (int(*)(int))binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     (int(*)(int))binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,    PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     (int(*)(int))binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     (int(*)(int))binary, PREC_FACTOR},
    [TOKEN_BANG]          = {NULL,     NULL,    PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     NULL,    PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,    PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     NULL,    PREC_NONE},
    [TOKEN_GREATER]       = {NULL,     NULL,    PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL,     NULL,    PREC_NONE},
    [TOKEN_LESS]          = {NULL,     NULL,    PREC_NONE},
    [TOKEN_LESS_EQUAL]    = {NULL,     NULL,    PREC_NONE},
    [TOKEN_IDENTIFIER]    = {variable, NULL,    PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,    PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,    PREC_NONE},
    [TOKEN_AND]           = {NULL,     NULL,    PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,    PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,    PREC_NONE},
    [TOKEN_FUNC]          = {NULL,     NULL,    PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,    PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,    PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,    PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,    PREC_NONE},
    [TOKEN_LOC]           = {NULL,     NULL,    PREC_NONE},
    [TOKEN_GLOBAL]        = {NULL,     NULL,    PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,    PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,    PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,    PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,    PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,    PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,    PREC_NONE},
};

/**
 * Returns the rule for a given token type.
 * This is used by the expression parser to determine 
 * how to handle tokens in infix/prefix positions.
 */
ParseRule* getRule(TokenType type) {
    return &rules[type];
}