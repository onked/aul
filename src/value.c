#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "value.h"
#include "object.h"
#include "memory.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = (oldCapacity < 8) ? 8 : oldCapacity * 2;
        array->values = (Value*)reallocate(array->values, sizeof(Value) * oldCapacity, sizeof(Value) * array->capacity);
    }
    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    if (array->values != NULL) {
        reallocate(array->values, sizeof(Value) * array->capacity, 0);
    }
    initValueArray(array);
}

void printValue(Value value) {
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_INTEGER(value)) {
        printf("%lld", (long long)AS_INTEGER(value));
    } else if (IS_NUMBER(value)) {
        double d = AS_NUMBER(value);
        if (d == (double)(int64_t)d) {
            printf("%.0f", d);
        } else {
            printf("%g", d);
        }
    } else if (IS_OBJ(value)) {
        if (IS_STRING(value)) {
            printf("%s", AS_CSTRING(value));
        } else if (IS_FUNCTION(value)) {
            ObjFunction* function = AS_FUNCTION(value);
            if (function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", function->name->chars);
            }
        } else if (IS_CLOSURE(value)) {
            ObjFunction* function = AS_CLOSURE(value)->function;
            if (function->name == NULL) {
                printf("<script>");
            } else {
                printf("<fn %s>", function->name->chars);
            }
        } else if (IS_TABLE(value)) {
            printf("<table>");
        }
    }
}

Value normalizeNumericKey(Value key) {
    if (IS_NUMBER(key)) {
        double d = AS_NUMBER_NC(key);
        if (d >= (double)INT48_MIN && d < (double)INT48_MAX + 1.0) {
            int64_t iv = (int64_t)d;
            if ((double)iv == d) return INTEGER_VAL(iv);
        }
    }
    return key;
}

bool valuesEqual(Value a, Value b) {
    if (a == b) return true;
    if (IS_NUMERIC(a) && IS_NUMERIC(b)) return valueToNumber(a) == valueToNumber(b);
    if (IS_OBJ(a) && IS_OBJ(b) && AS_OBJ(a)->type == AS_OBJ(b)->type) {
        if (IS_STRING(a)) {
            ObjString* sa = AS_STRING(a);
            ObjString* sb = AS_STRING(b);
            return sa->length == sb->length && memcmp(sa->chars, sb->chars, sa->length) == 0;
        }
        return AS_OBJ(a) == AS_OBJ(b);
    }
    return false;
}
