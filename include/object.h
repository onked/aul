#ifndef aul_object_h
#define aul_object_h

#include <stdint.h>
#include <stdbool.h>
#include "value.h"

typedef enum {
    OBJ_STRING,
} ObjType;

// This allows us to cast any object pointer to (Obj*) to check its type.
struct Obj {
    ObjType type;
    struct Obj* next; // For the Garbage Collector
};

struct ObjString {
    struct Obj obj; // The header (must be first)
    int length;
    char* chars;
    uint32_t hash;  // For fast Table lookups
};

// Helper Macros
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

// These macros cast a Value's internal pointer to the right struct type
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)

ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);

// Helper function to check object types safely
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// Macro for the check above
#define IS_STRING(value)    isObjType(value, OBJ_STRING)

#endif