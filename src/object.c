#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "object.h"
#include "value.h"
#include "vm.h"

// Helper to allocate memory for any object type
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)malloc(size);
    object->type = type;

    // Optional: Link this into a list of all objects for the Garbage Collector
    // object->next = vm.objects;
    // vm.objects = object;

    return object;
}

// The FNV-1a Hashing Algorithm
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

// Creates a new ObjString on the heap
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    return string;
}

// Use this when you want to "take ownership" of an existing C string
ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    return allocateString(chars, length, hash);
}

// Use this to copy a string (e.g., from the source code)
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    
    // Allocate a new buffer on the heap and copy the characters
    char* heapChars = malloc(length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}