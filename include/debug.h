#ifndef aul_debug_h
#define aul_debug_h

#include "chunk.h"

// Prints the whole chunk (all instructions)
void disassembleChunk(Chunk* chunk, const char* name);

// Prints a single instruction and returns the "offset" (where the next one starts)
int disassembleInstruction(Chunk* chunk, int offset);

#endif