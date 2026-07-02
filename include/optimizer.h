#ifndef aul_optimizer_h
#define aul_optimizer_h

#include "chunk.h"

void optimizeChunk(Chunk* chunk);
void foldCompareJumps(Chunk* chunk);
void removeNops(Chunk* chunk);

#endif
