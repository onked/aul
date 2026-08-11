#ifndef aul_table_h
#define aul_table_h

#include "value.h"
#include <stdint.h>

typedef struct ObjString ObjString;
typedef struct ObjTable ObjTable;

typedef struct {
    Value key;
    Value value;
} Entry;

typedef struct {
    int count;
    int capacity;
    uint32_t generation;
    Entry* entries;
} Table;

void objTablePromote(ObjTable* t);
bool objTableGet(ObjTable* t, Value key, Value* value);
bool objTableSet(ObjTable* t, Value key, Value value);

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, Value key, Value* value);
Entry* tableGetEntry(Table* table, Value key);
bool tableSet(Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif
