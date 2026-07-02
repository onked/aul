#ifndef aul_value_h
#define aul_value_h

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef uint64_t Value;

#define QNAN_TAG   0xFFF8000000000000ULL

#define NIL_VAL   (QNAN_TAG | 0x0ULL)
#define FALSE_VAL (QNAN_TAG | 0x1ULL)
#define TRUE_VAL  (QNAN_TAG | 0x2ULL)

// Integer tag: lower 3 bits = 101, stores int64_t in bits 3-50 (48-bit signed payload)
#define INT_TAG    0x5ULL
#define INT_DATA_MASK  0x0007FFFFFFFFFFFF8ULL
#define INT48_MIN  (-(1LL << 47))
#define INT48_MAX  ((1LL << 47) - 1)

static inline double valueToNumber(Value v) {
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

static inline Value numberToValue(double d) {
    Value v;
    memcpy(&v, &d, sizeof(v));
    return v;
}



#define TAG_MASK      (QNAN_TAG | 0x7ULL)
#define IS_NUMBER(v)  (((v) & QNAN_TAG) != QNAN_TAG)
#define IS_INTEGER(v) (((v) & TAG_MASK) == (QNAN_TAG | INT_TAG))
#define IS_NIL(v)     ((v) == NIL_VAL)
#define IS_BOOL(v)    ((v) == TRUE_VAL || (v) == FALSE_VAL)
#define IS_OBJ(v)     (((v) & TAG_MASK) == (QNAN_TAG | 0x3ULL))

#define AS_BOOL(v)    ((v) == TRUE_VAL)
#define AS_INTEGER(v) ((int64_t)((int64_t)((uint64_t)((v) & INT_DATA_MASK) << 13) >> 16))
#define AS_NUMBER(v)  (IS_INTEGER(v) ? (double)(AS_INTEGER(v)) : valueToNumber(v))
#define AS_NUMBER_NC(v) valueToNumber(v)
#define AS_OBJ(v)     ((Obj*)(uintptr_t)(((v) >> 3) & 0x0000FFFFFFFFFFFFULL))

#define BOOL_VAL(v)   ((v) ? TRUE_VAL : FALSE_VAL)
#define INTEGER_VAL(i) (QNAN_TAG | INT_TAG | (((uint64_t)(int64_t)(i) << 3) & INT_DATA_MASK))
#define NUMBER_VAL(v) numberToValue(v)
#define OBJ_VAL(p)    (QNAN_TAG | 0x3ULL | ((uint64_t)(uintptr_t)(p) << 3))

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
bool valuesEqual(Value a, Value b);

#endif
