#ifndef aul_value_h
#define aul_value_h

#include <string.h>
#include <stdbool.h>

// Forward declarations so we don't get circular include errors
typedef struct Obj Obj;
typedef struct ObjString ObjString;

// Define what TYPES of data our VM can hold
typedef enum {
  VAL_BOOL,
  VAL_NIL, 
  VAL_NUMBER,
  VAL_OBJ // This is a pointer to a heap-allocated object (like a String)
} ValueType;

// Tagged Union
typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj; 
  } as;
} Value;

// Checks
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

// Conversions
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)

// Initializers
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})



// Constant Pool (ValueArray)
typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

// Function prototypes for memory management
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif