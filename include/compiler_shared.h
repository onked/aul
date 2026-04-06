#ifndef aul_compiler_shared_h
#define aul_compiler_shared_h

#include <stdbool.h>
#include <stdint.h>

#include "scanner.h"
#include "chunk.h"
#include "object.h"
#include "value.h"

// Compiler & Local Variable Types
typedef struct {
    Token name;
    int depth;
    int reg;
} Local;

// Renamed to CompilerUpvalue to avoid conflict with object.h
typedef struct {
    uint8_t index;
    bool isLocal;
} CompilerUpvalue;

typedef struct Compiler {
    struct Compiler* enclosing; // Pointer to the parent function's compiler
    ObjFunction* function;      // The function object we are currently compiling

    Local locals[250];
    int localCount;
    
    CompilerUpvalue upvalues[250];      // Track which upvalues this function captures
    int upvalueCount;           
    
    int scopeDepth;
} Compiler;

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// Precedence & Rules
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

// Globals
extern Parser parser;
extern Compiler* current;
extern Chunk* compilingChunk;
extern int nextFreeRegister;

// Function Prototypes

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
int resolveLocalInCompiler(Compiler* compiler, Token* name); // Added for upvalue resolution

// compiler.c
void emitABC(OpCode op, int a, int b, int c);
void emitABx(OpCode op, int a, int bx);
uint16_t makeConstant(Value value);
int allocateRegister();
int emitJump(OpCode op);
void patchJump(int jumpPlaceholderOffset);
void declaration();
void statement();
int resolveUpvalue(Compiler* compiler, Token* name); // Added for closures

#endif