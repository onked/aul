#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"

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
    emitABx(OP_CONSTANT, reg, makeConstant(NUMBER_VAL(value)));
    return reg;
}

int string(bool canAssign) {
    (void)canAssign;
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
    int destReg = argReg;
    
    switch (operatorType) {
        case TOKEN_MINUS: emitABC(OP_NEGATE, destReg, argReg, 0); break;
        case TOKEN_BANG:
            emitABC(OP_NOT, destReg, argReg, 0);
            break;
        case TOKEN_HASH:
            destReg = allocateRegister();
            emitABC(OP_LENGTH, destReg, argReg, 0);
            break;
        default: return 0;
    }
    return destReg;
}

int binary(int leftReg) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    int rightReg = parsePrecedence((Precedence)(rule->precedence + 1));
    
    int destReg;
    
    switch (operatorType) {
        case TOKEN_PLUS:          destReg = leftReg; emitABC(OP_ADD,      destReg, leftReg, rightReg); break;
        case TOKEN_MINUS:         destReg = leftReg; emitABC(OP_SUBTRACT, destReg, leftReg, rightReg); break;
        case TOKEN_STAR:          destReg = leftReg; emitABC(OP_MULTIPLY, destReg, leftReg, rightReg); break;
        case TOKEN_SLASH:         destReg = leftReg; emitABC(OP_DIVIDE,   destReg, leftReg, rightReg); break;
        case TOKEN_EQUAL_EQUAL:   destReg = allocateRegister(); emitABC(OP_EQUAL,         destReg, leftReg, rightReg); break;
        case TOKEN_BANG_EQUAL:    destReg = allocateRegister(); emitABC(OP_EQUAL, destReg, leftReg, rightReg); emitABC(OP_NOT, destReg, destReg, 0); break;
        case TOKEN_GREATER:       destReg = allocateRegister(); emitABC(OP_GREATER,       destReg, leftReg, rightReg); break;
        case TOKEN_LESS:          destReg = allocateRegister(); emitABC(OP_LESS,          destReg, leftReg, rightReg); break;
        case TOKEN_GREATER_EQUAL: destReg = allocateRegister(); emitABC(OP_GREATER_EQUAL, destReg, leftReg, rightReg); break;
        case TOKEN_LESS_EQUAL:    destReg = allocateRegister(); emitABC(OP_LESS_EQUAL,    destReg, leftReg, rightReg); break;
        default: return 0;
    }

    nextFreeRegister--;
    return destReg;
}

int variable(bool canAssign) {
    Token name = parser.previous;
    
    // builtins
    if (name.length == 12 && memcmp(name.start, "setmetatable", 12) == 0) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'setmetatable'.");
        int tableReg = expression();
        consume(TOKEN_COMMA, "Expect ',' after table.");
        int mtReg = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after metatable.");
        emitABC(OP_SET_METATABLE, tableReg, mtReg, 0);
        return tableReg;
    }
    if (name.length == 12 && memcmp(name.start, "getmetatable", 12) == 0) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'getmetatable'.");
        int tableReg = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after table.");
        int destReg = allocateRegister();
        emitABC(OP_GET_METATABLE, destReg, tableReg, 0);
        return destReg;
    }
    
    int arg;
    OpCode getOp, setOp;

    arg = resolveLocal(&name);
    if (arg != -1) {
        getOp = OP_MOVE;
        setOp = OP_MOVE;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign) {
        if (match(TOKEN_PLUS_EQUAL) || match(TOKEN_MINUS_EQUAL) ||
            match(TOKEN_STAR_EQUAL) || match(TOKEN_SLASH_EQUAL)) {
            TokenType op = parser.previous.type;
            int valReg = expression();
            int resultReg = allocateRegister();
            
            if (getOp == OP_MOVE) {
                if (op == TOKEN_PLUS_EQUAL) {
                    emitABC(OP_ADD, resultReg, arg, valReg);
                } else if (op == TOKEN_MINUS_EQUAL) {
                    emitABC(OP_SUBTRACT, resultReg, arg, valReg);
                } else if (op == TOKEN_STAR_EQUAL) {
                    emitABC(OP_MULTIPLY, resultReg, arg, valReg);
                } else {
                    emitABC(OP_DIVIDE, resultReg, arg, valReg);
                }
                emitABC(OP_MOVE, arg, resultReg, 0);
            } else {
                int loadReg = allocateRegister();
                emitABC(getOp, loadReg, arg, 0);
                if (op == TOKEN_PLUS_EQUAL) {
                    emitABC(OP_ADD, resultReg, loadReg, valReg);
                } else if (op == TOKEN_MINUS_EQUAL) {
                    emitABC(OP_SUBTRACT, resultReg, loadReg, valReg);
                } else if (op == TOKEN_STAR_EQUAL) {
                    emitABC(OP_MULTIPLY, resultReg, loadReg, valReg);
                } else {
                    emitABC(OP_DIVIDE, resultReg, loadReg, valReg);
                }
                emitABC(setOp, arg, resultReg, 0);
            }
            nextFreeRegister--;
            return resultReg;
        }
        
        if (match(TOKEN_EQUAL)) {
            int valReg = expression();
            if (getOp == OP_MOVE) {
                emitABC(OP_MOVE, arg, valReg, 0);
            } else {
                emitABC(setOp, arg, valReg, 0);
            }
            return valReg;
        }
        
        if (match(TOKEN_PLUS_PLUS)) {
            int oneReg = allocateRegister();
            emitABx(OP_CONSTANT, oneReg, makeConstant(NUMBER_VAL(1)));
            int resultReg = allocateRegister();
            
            if (getOp == OP_MOVE) {
                emitABC(OP_ADD, resultReg, arg, oneReg);
                emitABC(OP_MOVE, arg, resultReg, 0);
            } else {
                int loadReg = allocateRegister();
                emitABC(getOp, loadReg, arg, 0);
                emitABC(OP_ADD, resultReg, loadReg, oneReg);
                emitABC(setOp, arg, resultReg, 0);
            }
            nextFreeRegister -= 2;
            return resultReg;
        }
    }
    
    if (getOp == OP_MOVE) return arg;

    int destReg = allocateRegister();
    emitABC(getOp, destReg, arg, 0);
    return destReg;
}

int tableLiteral(bool canAssign) {
    (void)canAssign;
    int tableReg = allocateRegister();
    emitABC(OP_TABLE, tableReg, 0, 0);
    
    if (tableReg > current->maxRegister) {
        current->maxRegister = tableReg;
    }
    
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            uint16_t keyConstant;
            
            if (check(TOKEN_NUMBER)) {
                advance();
                double value = strtod(parser.previous.start, NULL);
                keyConstant = makeConstant(NUMBER_VAL(value));
            } else if (check(TOKEN_STRING)) {
                advance();
                keyConstant = makeConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
            } else if (check(TOKEN_IDENTIFIER)) {
                advance();
                keyConstant = makeConstant(OBJ_VAL(copyString(parser.previous.start, parser.previous.length)));
            } else {
                errorAt(&parser.current, "Expect table key.");
                return tableReg;
            }
            
            consume(TOKEN_COLON, "Expect ':' after table key.");
            
            int valReg = expression();
            int keyReg = allocateRegister();
            
            emitABx(OP_CONSTANT, keyReg, keyConstant);
            emitABC(OP_SET_TABLE, tableReg, keyReg, valReg);
            
            nextFreeRegister = tableReg + 1;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after table entries.");
    return tableReg;
}

int subscript(int leftReg) {
    int keyReg = expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after subscript.");
    
    if (match(TOKEN_EQUAL)) {
        int valReg = expression();
        emitABC(OP_SET_TABLE, leftReg, keyReg, valReg);
        nextFreeRegister -= 2;
        return leftReg;
    }
    
    int destReg = allocateRegister();
    emitABC(OP_GET_TABLE, destReg, leftReg, keyReg);
    nextFreeRegister--;
    return destReg;
}

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

int and_(int leftReg) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    uint32_t inst = compilingChunk->code[endJump];
    compilingChunk->code[endJump] = CREATE_ABx(OP_JUMP_IF_FALSE, leftReg, GET_Bx(inst));
    
    int rightReg = expression();
    patchJump(endJump);
    return rightReg;
}

int or_(int leftReg) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    uint32_t inst = compilingChunk->code[elseJump];
    compilingChunk->code[elseJump] = CREATE_ABx(OP_JUMP_IF_FALSE, leftReg, GET_Bx(inst));
    
    int endJump = emitJump(OP_JUMP);
    patchJump(elseJump);
    
    int rightReg = expression();
    patchJump(endJump);
    return rightReg;
}

static int resolveLocal(Token* name) {
    return resolveLocalInCompiler(current, name);
}
