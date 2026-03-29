#ifndef aul_compiler_h
#define aul_compiler_h

#include "chunk.h"

// Returns true if compilation succeeded
bool compile(const char* source, Chunk* chunk);

#endif