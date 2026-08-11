#ifndef aul_object_h
#define aul_object_h

#include "value.h"
#include "chunk.h"
#include "table.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_TABLE,
    OBJ_NATIVE
} ObjType;

typedef void (*NativeFn)(int argCount, Value* args, Value* result);

#define GC_COLOR_WHITE0  0
#define GC_COLOR_WHITE1  1
#define GC_COLOR_BLACK   2
#define GC_COLOR_GRAY    3
#define GC_COLOR_MASK    3

struct Obj {
    ObjType type;
    uint8_t marked;
    struct Obj* next;
};

typedef struct {
    uint8_t index;
    bool isLocal;
    bool readonly;
} Upvalue;

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    int maxRegs; // number of registers this function uses (for GC scanning)
    Chunk chunk;
    struct ObjString* name;
    Upvalue upvalues[250];
} ObjFunction;

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    Value* readonlyValues;
    int upvalueCount;
} ObjClosure;

#define TABLE_INLINE_CAPACITY 4

typedef struct ObjTable {
    Obj obj;
    int arrayCapacity;
    Value* array;
    int inlineCount;
    Value inlineKeys[TABLE_INLINE_CAPACITY];
    Value inlineVals[TABLE_INLINE_CAPACITY];
    Table fields;
    struct ObjTable* metatable;
    uint32_t writeGen;
    uint32_t metaGen;
    Value cachedIndex;
    Value cachedNewIndex;
    Value cachedCall;
    Value cachedLen;
    struct ObjTable* gclist;
} ObjTable;

typedef struct {
    Obj obj;
    NativeFn function;
    ObjString* name;
    int arity;
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

#define OBJ_TYPE(value)     (AS_OBJ(value)->type)
#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_TABLE(value)     isObjType(value, OBJ_TABLE)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)

#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value))
#define AS_TABLE(value)     ((ObjTable*)AS_OBJ(value))
#define AS_NATIVE(value)    ((ObjNative*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjFunction* newFunction();
ObjNative* newNative(const char* name, NativeFn function, int arity);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjTable* newTable();
ObjString* copyString(const char* chars, int length);
ObjString* takeString(char* chars, int length);

#endif