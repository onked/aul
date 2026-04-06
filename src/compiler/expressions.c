#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"

// Forward declaration so we can use it in variable()
static int resolveLocal(Token* name);

int parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        errorAt(&parser.previous, "Expect expression.");
        return 0;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    int leftReg = prefixRule(canAssign);

    // Keep eating tokens as long as they have higher priority than the current op
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        int (*infixRule)(int) = (int (*)(int))getRule(parser.previous.type)->infix;
        if (infixRule != NULL) {
            leftReg = infixRule(leftReg);
        }
    }

    return leftReg;
}

int expression() {
    return parsePrecedence(PREC_ASSIGNMENT);
}

int number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    int reg = allocateRegister();
    // Shove the number into the constant pool and emit the instruction
    emitABx(OP_CONSTANT, reg, makeConstant(NUMBER_VAL(value)));
    return reg;
}

int string(bool canAssign) {
    (void)canAssign;
    // Strip the quotes and copy the string data
    Value value = OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2));
    int reg = allocateRegister();
    emitABx(OP_CONSTANT, reg, makeConstant(value));
    return reg;
}

int literal(bool canAssign) {
    (void)canAssign;
    int reg = allocateRegister();
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitABC(OP_FALSE, reg, 0, 0); break;
        case TOKEN_NIL:   emitABC(OP_NIL,   reg, 0, 0); break;
        case TOKEN_TRUE:  emitABC(OP_TRUE,  reg, 0, 0); break;
        default: return 0;
    }
    return reg;
}

int grouping(bool canAssign) {
    (void)canAssign;
    int reg = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return reg;
}

int unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    int argReg = parsePrecedence(PREC_UNARY);
    
    int destReg = argReg; // Just reuse the register we already have
    
    switch (operatorType) {
        case TOKEN_MINUS: emitABC(OP_NEGATE, destReg, argReg, 0); break;
        default: return 0;
    }
    return destReg;
}

int binary(int leftReg) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    // Parse the right side with slightly higher precedence to handle nesting
    int rightReg = parsePrecedence((Precedence)(rule->precedence + 1));
    
    int destReg = leftReg; 
    
    switch (operatorType) {
        case TOKEN_PLUS:   emitABC(OP_ADD,      destReg, leftReg, rightReg); break;
        case TOKEN_MINUS:  emitABC(OP_SUBTRACT, destReg, leftReg, rightReg); break;
        case TOKEN_STAR:   emitABC(OP_MULTIPLY, destReg, leftReg, rightReg); break;
        case TOKEN_SLASH:  emitABC(OP_DIVIDE,   destReg, leftReg, rightReg); break;
        default: return 0;
    }

    // Done with the right-side temp register, so free it up
    nextFreeRegister--; 
    return destReg;
}

int variable(bool canAssign) {
    Token name = parser.previous;
    int arg;
    OpCode getOp, setOp;

    // First: Is it a local variable in the current function?
    arg = resolveLocal(&name);
    if (arg != -1) {
        getOp = OP_MOVE; 
        setOp = OP_MOVE;
    } 
    // Second: Check if it belongs to an outer function (an upvalue)
    else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } 
    // Third: If we can't find it anywhere else, it's gotta be a global
    else {
        arg = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    // Handle assignments like 'x = 10'
    if (canAssign && match(TOKEN_EQUAL)) {
        int valReg = expression();
        if (getOp == OP_MOVE) {
            emitABC(OP_MOVE, arg, valReg, 0);
        } else {
            // For upvalues/globals, A is the index and B is where the value lives
            emitABC(setOp, arg, valReg, 0); 
        }
        return valReg;
    } else {
        // If it's a local, we don't need to 'get' it, it's already in a register
        if (getOp == OP_MOVE) return arg; 

        int destReg = allocateRegister();
        // Load the upvalue or global into a fresh register
        emitABC(getOp, destReg, arg, 0);
        return destReg;
    }
}

// Scans the locals of a specific compiler/function for a variable name
int resolveLocalInCompiler(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (name->length == local->name.length &&
            memcmp(name->start, local->name.start, name->length) == 0) {
            return local->reg;
        }
    }
    return -1;
}

// Shortcut to look for locals in whatever function we are currently compiling
static int resolveLocal(Token* name) {
    return resolveLocalInCompiler(current, name);
}