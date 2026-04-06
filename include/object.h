#ifndef aul_object_h
#define aul_object_h

#include "value.h"
#include "chunk.h"

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE
} ObjType;

struct Obj {
    ObjType type;
    struct Obj* next;
};

// Metadata for the compiler to tell the VM which variables to capture
typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    struct ObjString* name;
    Upvalue upvalues[250]; // The "map" used to build closures at runtime
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
    int upvalueCount;
} ObjClosure;

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

// Macros
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)
#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)

#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)

#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// Prototypes
ObjFunction* newFunction();
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjString* copyString(const char* chars, int length);
ObjString* takeString(char* chars, int length);

#endif