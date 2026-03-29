#ifndef aul_value_h
#define aul_value_h

#include <string.h>

// For now, we use a simple double, but OP_DEFINE_GLOBAL 
// needs to know the NAME of the variable.
typedef double Value;

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);

#endif