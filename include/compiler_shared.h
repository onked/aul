#ifndef aul_compiler_shared_h
#define aul_compiler_shared_h

#include <stdbool.h>
#include <stdint.h>

#include "scanner.h"
#include "chunk.h"
#include "object.h"
#include "value.h"

typedef struct {
    Token name;
    int depth;
    int reg;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} CompilerUpvalue;

typedef struct Loop {
    int continueOffset;
    int breakJump;
    struct Loop* enclosing;
} Loop;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;

    Local locals[250];
    int localCount;
    int maxRegister;
    
    CompilerUpvalue upvalues[250];
    int upvalueCount;
    
    int scopeDepth;
    Loop* currentLoop;
} Compiler;

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef int (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    int (*infix)(int leftReg);
    Precedence precedence;
} ParseRule;

extern Parser parser;
extern Compiler* current;
extern Chunk* compilingChunk;
extern int nextFreeRegister;

// parser.c
void errorAt(Token* token, const char* message);
void advance();
void consume(TokenType type, const char* message);
bool check(TokenType type);
bool match(TokenType type);
void synchronize();

// rules.c
ParseRule* getRule(TokenType type);

// expressions.c
int expression();
int parsePrecedence(Precedence precedence);
int number(bool canAssign);
int string(bool canAssign);
int grouping(bool canAssign);
int unary(bool canAssign);
int binary(int leftReg);
int variable(bool canAssign);
int literal(bool canAssign);
int call(int leftReg);
int and_(int leftReg);
int or_(int leftReg);
int tableLiteral(bool canAssign);
int subscript(int leftReg);
int functionExpr(bool canAssign);
int resolveLocalInCompiler(Compiler* compiler, Token* name);

// compiler.c
void emitABC(OpCode op, int a, int b, int c);
void emitABx(OpCode op, int a, int bx);
uint16_t makeConstant(Value value);
int allocateRegister(void);
int emitJump(OpCode op);
void patchJump(int jumpPlaceholderOffset);
void initCompiler(Compiler* compiler, Compiler* enclosing);
ObjFunction* endCompiler(void);
void addLocal(Token name, int reg);
int resolveUpvalue(Compiler* compiler, Token* name);

// statements.c
void statement(void);
void declaration(void);

#endif
