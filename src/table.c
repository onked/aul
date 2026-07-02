#include <stdlib.h>
#include <string.h>

#include "table.h"
#include "object.h"
#include "value.h"
#include "memory.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    if (table->entries != NULL) {
        reallocate(table->entries, sizeof(Entry) * table->capacity, 0);
    }
    initTable(table);
}

static uint32_t hashValue(Value key) {
    if (IS_STRING(key)) {
        return AS_STRING(key)->hash;
    }
    if (IS_INTEGER(key)) {
        return (uint32_t)(AS_INTEGER(key));
    }
    double d = valueToNumber(key);
    return (uint32_t)((int64_t)d ^ (int64_t)(d * 0x9e3779b97f4a7c15ULL));
}

static Entry* findEntry(Entry* entries, int capacity, Value key) {
    uint32_t mask = capacity - 1;
    uint32_t index = hashValue(key) & mask;
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        if (IS_NIL(entry->key)) {
            if (IS_BOOL(entry->value) && AS_BOOL(entry->value)) {
                if (tombstone == NULL) tombstone = entry;
            } else {
                return tombstone != NULL ? tombstone : entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }

        index = (index + 1) & mask;
    }
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = (Entry*)reallocate(NULL, 0, sizeof(Entry) * capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (IS_NIL(entry->key)) continue;

        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    if (table->entries != NULL) {
        reallocate(table->entries, sizeof(Entry) * table->capacity, 0);
    }
    table->entries = entries;
    table->capacity = capacity;
}

bool tableSet(Table* table, Value key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = (table->capacity < 8) ? 8 : table->capacity * 2;
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = IS_NIL(entry->key);
    if (isNewKey && (!IS_BOOL(entry->value) || !AS_BOOL(entry->value))) table->count++;

    entry->key = key;
    entry->value = value;
    gcWriteBarrier(value);
    return isNewKey;
}

bool tableGet(Table* table, Value key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;

    *value = entry->value;
    return true;
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->capacity == 0) return NULL;
    uint32_t index = hash % table->capacity;
    for (;;) {
        Entry* entry = &table->entries[index];
        if (IS_NIL(entry->key)) {
            if (IS_BOOL(entry->value) && AS_BOOL(entry->value)) {
                // tombstone
            } else {
                return NULL;
            }
        } else if (IS_STRING(entry->key)) {
            ObjString* str = AS_STRING(entry->key);
            if (str->length == length &&
                str->hash == hash &&
                memcmp(str->chars, chars, length) == 0) {
                return str;
            }
        }
        index = (index + 1) % table->capacity;
    }
}

bool tableDelete(Table* table, Value key) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;

    entry->key = NIL_VAL;
    entry->value = BOOL_VAL(true);
    return true;
}

void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (!IS_NIL(entry->key)) {
            tableSet(to, entry->key, entry->value);
        }
    }
}
