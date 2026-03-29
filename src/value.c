#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "value.h"
#include "object.h" // We need this to print strings later

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = (oldCapacity < 8) ? 8 : oldCapacity * 2;
        array->values = (Value*)realloc(array->values, sizeof(Value) * array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    free(array->values);
    initValueArray(array);
}

// Translator
void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL:   printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL:    printf("nil"); break;
        case VAL_NUMBER: printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: {
            // This is how we'll print strings once we wire up object.c
            if (IS_STRING(value)) {
                printf("%s", AS_CSTRING(value));
            }
            break;
        }
    }
}