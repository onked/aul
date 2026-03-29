#ifndef aul_scanner_h
#define aul_scanner_h

typedef enum {
  // Single-character tokens
  TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
  TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, 
  TOKEN_PLUS, TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,

  /// Comparison operators
  TOKEN_BANG, TOKEN_BANG_EQUAL,
  TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER, TOKEN_GREATER_EQUAL,
  TOKEN_LESS, TOKEN_LESS_EQUAL,

  // Literals
  TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

  // Keywords (The "Brains" of Aul)
  TOKEN_IF, TOKEN_ELSE, TOKEN_FUNC, TOKEN_LOC, 
  TOKEN_RETURN, TOKEN_TRUE, TOKEN_FALSE,
  TOKEN_FOR, TOKEN_WHILE, TOKEN_PRINT,
  TOKEN_AND, TOKEN_OR, TOKEN_NOT, TOKEN_GLOBAL,

  TOKEN_ERROR, // Useful for when the scanner finds something weird
  TOKEN_EOF    // End of File
} TokenType;

typedef struct {
  TokenType type;    // The enum value (e.g., TOKEN_NUMBER)
  const char* start; // Pointer to the first character of the token in your code
  int length;        // How many characters long the token is
  int line;          // What line number it's on (for error reporting)
} Token;

typedef struct {
  const char* start;   // The very beginning of the current token
  const char* current; // The character we are currently looking at
  int line;            // The current line we are scanning
} Scanner;

void initScanner(const char* source);
Token scanToken();

#endif