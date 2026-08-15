#include <stdlib.h>
#include <string.h>

#include "compiler_shared.h"
#include "compiler.h"

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
    lastCallReg = -1;
    return parsePrecedence(PREC_ASSIGNMENT);
}

int number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    int reg = allocateRegister();
    if (value == (double)(int64_t)value && value >= INT48_MIN && value <= INT48_MAX) {
        emitABx(OP_CONSTANT, reg, makeConstant(INTEGER_VAL((int64_t)value)));
    } else {
        emitABx(OP_CONSTANT, reg, makeConstant(NUMBER_VAL(value)));
    }
    lastCallReg = -1;
    return reg;
}

static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static ObjString* decodeStringLiteral(Token token) {
    const char* src = token.start + 1;
    int len = token.length - 2;
    bool hasEscape = false;
    for (int i = 0; i < len; i++) {
        if (src[i] == '\\') {
            hasEscape = true;
            break;
        }
    }
    if (!hasEscape) {
        return copyString(src, len);
    }

    char* buf = (char*)malloc(len + 1);
    int n = 0;
    int i = 0;
    while (i < len) {
        char c = src[i];
        if (c == '\\' && i + 1 < len) {
            char e = src[i + 1];
            switch (e) {
                case 'n': buf[n++] = '\n'; i += 2; continue;
                case 't': buf[n++] = '\t'; i += 2; continue;
                case 'r': buf[n++] = '\r'; i += 2; continue;
                case '0': buf[n++] = '\0'; i += 2; continue;
                case 'a': buf[n++] = '\a'; i += 2; continue;
                case 'b': buf[n++] = '\b'; i += 2; continue;
                case 'f': buf[n++] = '\f'; i += 2; continue;
                case 'v': buf[n++] = '\v'; i += 2; continue;
                case '"': buf[n++] = '"';  i += 2; continue;
                case '\'': buf[n++] = '\''; i += 2; continue;
                case '\\': buf[n++] = '\\'; i += 2; continue;
                case 'x':
                    if (i + 3 < len && isHexDigit(src[i + 2]) && isHexDigit(src[i + 3])) {
                        int hi = 0;
                        if (src[i + 2] >= '0' && src[i + 2] <= '9') hi = src[i + 2] - '0';
                        else if (src[i + 2] >= 'a' && src[i + 2] <= 'f') hi = src[i + 2] - 'a' + 10;
                        else hi = src[i + 2] - 'A' + 10;
                        int lo = 0;
                        if (src[i + 3] >= '0' && src[i + 3] <= '9') lo = src[i + 3] - '0';
                        else if (src[i + 3] >= 'a' && src[i + 3] <= 'f') lo = src[i + 3] - 'a' + 10;
                        else lo = src[i + 3] - 'A' + 10;
                        buf[n++] = (char)(hi * 16 + lo);
                        i += 4;
                        continue;
                    }
                    buf[n++] = 'x';
                    i += 2;
                    continue;
                default:
                    buf[n++] = e;
                    i += 2;
                    continue;
            }
        }
        buf[n++] = c;
        i += 1;
    }
    buf[n] = '\0';
    return takeString(buf, n);
}

int string(bool canAssign) {
    (void)canAssign;
    Value value = OBJ_VAL(decodeStringLiteral(parser.previous));
    int reg = allocateRegister();
    emitABx(OP_CONSTANT, reg, makeConstant(value));
    lastCallReg = -1;
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
    lastCallReg = -1;
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
    lastCallReg = -1;
    return destReg;
}

int binary(int leftReg) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    int rightReg = parsePrecedence((Precedence)(rule->precedence + 1));
    
    int destReg = allocateRegister();
    
    switch (operatorType) {
        case TOKEN_PLUS:
            if (regIsBoundToLocal(leftReg)) {
                destReg = allocateRegister();
                emitABC(OP_ADD_BUF, destReg, leftReg, rightReg);
            } else {
                emitABC(OP_ADD_BUF, leftReg, leftReg, rightReg);
                destReg = leftReg;
            }
            break;
        case TOKEN_MINUS:         emitABC(OP_SUBTRACT, destReg, leftReg, rightReg); break;
        case TOKEN_STAR:          emitABC(OP_MULTIPLY, destReg, leftReg, rightReg); break;
        case TOKEN_SLASH:         emitABC(OP_DIVIDE,   destReg, leftReg, rightReg); break;
        case TOKEN_EQUAL_EQUAL:   emitABC(OP_EQUAL,     destReg, leftReg, rightReg); break;
        case TOKEN_BANG_EQUAL:    emitABC(OP_NOT_EQUAL, destReg, leftReg, rightReg); break;
        case TOKEN_GREATER:       emitABC(OP_GREATER,       destReg, leftReg, rightReg); break;
        case TOKEN_LESS:          emitABC(OP_LESS,          destReg, leftReg, rightReg); break;
        case TOKEN_GREATER_EQUAL: emitABC(OP_GREATER_EQUAL, destReg, leftReg, rightReg); break;
        case TOKEN_LESS_EQUAL:    emitABC(OP_LESS_EQUAL,    destReg, leftReg, rightReg); break;
        case TOKEN_PERCENT:       emitABC(OP_MODULO,    destReg, leftReg, rightReg); break;
        default: return 0;
    }

    lastCallReg = -1;
    return destReg;
}

static void emitGet(OpCode op, int destReg, int arg) {
    if (op == OP_GET_GLOBAL) {
        emitABx(op, destReg, arg);
    } else {
        emitABC(op, destReg, arg, 0);
    }
}

static void emitSet(OpCode op, int arg, int valReg) {
    if (op == OP_SET_GLOBAL) {
        emitABx(op, valReg, arg);
    } else {
        emitABC(op, arg, valReg, 0);
    }
}

int variable(bool canAssign) {
    Token name = parser.previous;
    
    // builtins
    if (name.length == 5 && memcmp(name.start, "clock", 5) == 0) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'clock'.");
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'clock'.");
        int destReg = allocateRegister();
        emitABC(OP_CLOCK, destReg, 0, 0);
        return destReg;
    }
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
    if (name.length == 4 && memcmp(name.start, "sqrt", 4) == 0) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'sqrt'.");
        int argReg = expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after argument.");
        int destReg = allocateRegister();
        emitABC(OP_SQRT, destReg, argReg, 0);
        return destReg;
    }



    int arg;
    OpCode getOp, setOp;
    bool upvalueReadonly = false;

    arg = resolveLocal(&name);
    if (arg != -1) {
        getOp = OP_MOVE;
        setOp = OP_MOVE;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        upvalueReadonly = current->upvalues[arg].readonly;
        getOp = upvalueReadonly ? OP_GET_READONLY_UPVALUE : OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign) {
        // mark local/upvalue as mutated when there's any assignment
        if (setOp == OP_MOVE) {
            for (int k = 0; k < current->localCount; k++) {
                if (current->locals[k].reg == arg) {
                    current->locals[k].mutated = true;
                    break;
                }
            }
        } else if (setOp == OP_SET_UPVALUE) {
            current->upvalues[arg].readonly = false;
            Compiler* c = current->enclosing;
            uint8_t localReg = current->upvalues[arg].index;
            while (c) {
                if (current->upvalues[arg].isLocal) {
                    for (int k = 0; k < c->localCount; k++) {
                        if ((uint8_t)c->locals[k].reg == localReg) {
                            c->locals[k].mutated = true;
                            goto mutatedDone;
                        }
                    }
                } else {
                    localReg = c->upvalues[localReg].index;
                    c = c->enclosing;
                    continue;
                }
                break;
            }
            mutatedDone:; }
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
                emitGet(getOp, loadReg, arg);
                if (op == TOKEN_PLUS_EQUAL) {
                    emitABC(OP_ADD, resultReg, loadReg, valReg);
                } else if (op == TOKEN_MINUS_EQUAL) {
                    emitABC(OP_SUBTRACT, resultReg, loadReg, valReg);
                } else if (op == TOKEN_STAR_EQUAL) {
                    emitABC(OP_MULTIPLY, resultReg, loadReg, valReg);
                } else {
                    emitABC(OP_DIVIDE, resultReg, loadReg, valReg);
                }
                emitSet(setOp, arg, resultReg);
            }
            nextFreeRegister--;
            return resultReg;
        }
        
        if (match(TOKEN_EQUAL)) {
            int valReg = expression();
            if (getOp == OP_MOVE) {
                emitABC(OP_MOVE, arg, valReg, 0);
            } else {
                emitSet(setOp, arg, valReg);
            }
            return valReg;
        }
        
        if (match(TOKEN_PLUS_PLUS)) {
            int oneReg = allocateRegister();
            emitABx(OP_CONSTANT, oneReg, makeConstant(INTEGER_VAL(1)));
            int resultReg = allocateRegister();
            
            if (getOp == OP_MOVE) {
                emitABC(OP_ADD, resultReg, arg, oneReg);
                emitABC(OP_MOVE, arg, resultReg, 0);
            } else {
                int loadReg = allocateRegister();
                emitGet(getOp, loadReg, arg);
                emitABC(OP_ADD, resultReg, loadReg, oneReg);
                emitSet(setOp, arg, resultReg);
            }
            nextFreeRegister -= 2;
            return resultReg;
        }
    }
    
    if (getOp == OP_MOVE) return arg;

    int destReg = allocateRegister();
    emitGet(getOp, destReg, arg);
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
        int autoIndex = 1;
        do {
            if (check(TOKEN_RIGHT_BRACE)) break;
            uint16_t keyConstant = 0;
            bool keyed = false;
            bool keyedNumber = false;
            int64_t keyNum = 0;

            if (check(TOKEN_NUMBER) || check(TOKEN_STRING) || check(TOKEN_IDENTIFIER)) {
                Scanner savedScanner = scanner;
                Token savedPrev = parser.previous;
                Token savedCurr = parser.current;
                advance();
                Token keyToken = parser.previous;
                if (match(TOKEN_COLON)) {
                    if (keyToken.type == TOKEN_NUMBER) {
                        double value = strtod(keyToken.start, NULL);
                        if (value == (double)(int64_t)value && value >= INT48_MIN && value <= INT48_MAX) {
                            keyConstant = makeConstant(INTEGER_VAL((int64_t)value));
                            keyedNumber = true;
                            keyNum = (int64_t)value;
                        } else {
                            keyConstant = makeConstant(NUMBER_VAL(value));
                        }
                    } else if (keyToken.type == TOKEN_STRING) {
                        keyConstant = makeConstant(OBJ_VAL(decodeStringLiteral(keyToken)));
                    } else {
                        keyConstant = makeConstant(OBJ_VAL(copyString(keyToken.start, keyToken.length)));
                    }
                    keyed = true;
                } else {
                    scanner = savedScanner;
                    parser.previous = savedPrev;
                    parser.current = savedCurr;
                }
            }
            
            if (keyed && keyedNumber && keyNum == autoIndex) {
                autoIndex = keyNum + 1;
            }
            
            int valReg = expression();
            int keyReg = allocateRegister();
            if (!keyed) {
                keyConstant = makeConstant(INTEGER_VAL(autoIndex++));
            }
            
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
        int saveReg = allocateRegister();
        emitABC(OP_MOVE, saveReg, keyReg, 0);
        nextFreeRegister = saveReg + 1;
        int valReg = expression();
        emitABC(OP_SET_TABLE, leftReg, saveReg, valReg);
        nextFreeRegister = saveReg;
        return leftReg;
    }
    
    int destReg = allocateRegister();
    emitABC(OP_GET_TABLE, destReg, leftReg, keyReg);
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
    lastCallReg = -1;
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
    lastCallReg = -1;
    return rightReg;
}

int dotAccess(int leftReg) {
    consume(TOKEN_IDENTIFIER, "Expect identifier after '.'.");
    Token name = parser.previous;
    int keyReg = allocateRegister();
    emitABx(OP_CONSTANT, keyReg, makeConstant(OBJ_VAL(copyString(name.start, name.length))));


    if (match(TOKEN_EQUAL)) {
        int valReg = expression();
        emitABC(OP_SET_TABLE, leftReg, keyReg, valReg);
        nextFreeRegister = keyReg;
        return leftReg;
    }

    int destReg = allocateRegister();
    emitABC(OP_GET_TABLE, destReg, leftReg, keyReg);
    return destReg;
}

static int resolveLocal(Token* name) {
    return resolveLocalInCompiler(current, name);
}
