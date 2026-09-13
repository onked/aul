#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "libs/io.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"

static bool isFileValue(Value v) {
    return IS_FILE(v);
}

static void ioOpen(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = NIL_VAL; return; }
    ObjString* path = AS_STRING(args[0]);
    const char* mode = "r";
    if (argCount >= 2 && IS_STRING(args[1])) {
        mode = AS_STRING(args[1])->chars;
    }
    FILE* f = fopen(path->chars, mode);
    if (!f) { *result = NIL_VAL; return; }
    ObjFile* file = newFile(f, path);
    *result = OBJ_VAL(file);
}

static void ioClose(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !isFileValue(args[0])) { *result = NIL_VAL; return; }
    ObjFile* f = AS_FILE(args[0]);
    if (f->closed || !f->file) { *result = NIL_VAL; return; }
    fclose(f->file);
    f->file = NULL;
    f->closed = true;
    *result = TRUE_VAL;
}

static void ioFlush(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !isFileValue(args[0])) { *result = NIL_VAL; return; }
    ObjFile* f = AS_FILE(args[0]);
    if (f->closed || !f->file) { *result = NIL_VAL; return; }
    fflush(f->file);
    *result = TRUE_VAL;
}

static void ioSeek(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !isFileValue(args[0])) { *result = NIL_VAL; return; }
    ObjFile* f = AS_FILE(args[0]);
    if (f->closed || !f->file) { *result = NIL_VAL; return; }
    const char* whenceStr = "cur";
    long offset = 0;
    if (argCount >= 2 && IS_STRING(args[1])) whenceStr = AS_STRING(args[1])->chars;
    if (argCount >= 3 && IS_INTEGER(args[2])) offset = (long)AS_INTEGER(args[2]);
    else if (argCount >= 3 && IS_NUMBER(args[2])) offset = (long)AS_NUMBER(args[2]);
    int whence = SEEK_CUR;
    if (strcmp(whenceStr, "set") == 0) whence = SEEK_SET;
    else if (strcmp(whenceStr, "cur") == 0) whence = SEEK_CUR;
    else if (strcmp(whenceStr, "end") == 0) whence = SEEK_END;
    if (fseek(f->file, offset, whence) != 0) { *result = NIL_VAL; return; }
    long pos = ftell(f->file);
    *result = INTEGER_VAL(pos);
}

static void ioRead(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !isFileValue(args[0])) { *result = NIL_VAL; return; }
    ObjFile* f = AS_FILE(args[0]);
    if (f->closed || !f->file) { *result = NIL_VAL; return; }

    if (argCount == 1) {
        size_t cap = 4096;
        size_t len = 0;
        char* buf = (char*)reallocate(NULL, 0, cap);
        size_t n;
        while ((n = fread(buf + len, 1, cap - len, f->file)) > 0) {
            len += n;
            if (len == cap) {
                size_t oldCap = cap;
                cap *= 2;
                buf = (char*)reallocate(buf, oldCap, cap);
            }
        }
        Value s = OBJ_VAL(copyString(buf, (int)len));
        reallocate(buf, cap, 0);
        *result = s;
        return;
    }
    Value mode = args[1];
    if (IS_INTEGER(mode)) {
        int64_t n = AS_INTEGER(mode);
        if (n < 0) n = 0;
        if (n == 0) { *result = OBJ_VAL(copyString("",0)); return; }
        char* buf = (char*)reallocate(NULL, 0, (size_t)n);
        size_t read = fread(buf, 1, (size_t)n, f->file);
        Value s = OBJ_VAL(copyString(buf, (int)read));
        reallocate(buf, (size_t)n, 0);
        *result = s;
        return;
    }
    if (IS_NUMBER(mode)) {
        int64_t n = (int64_t)AS_NUMBER(mode);
        if (n < 0) n = 0;
        if (n == 0) { *result = OBJ_VAL(copyString("",0)); return; }
        char* buf = (char*)reallocate(NULL, 0, (size_t)n);
        size_t read = fread(buf, 1, (size_t)n, f->file);
        Value s = OBJ_VAL(copyString(buf, (int)read));
        reallocate(buf, (size_t)n, 0);
        *result = s;
        return;
    }
    if (IS_STRING(mode)) {
        const char* m = AS_STRING(mode)->chars;
        if (strcmp(m, "*a") == 0 || strcmp(m, "*all") == 0 || strcmp(m, "a") == 0) {
            size_t cap = 4096;
            size_t len = 0;
            char* buf = (char*)reallocate(NULL, 0, cap);
            size_t n;
            while ((n = fread(buf + len, 1, cap - len, f->file)) > 0) {
                len += n;
                if (len == cap) {
                    size_t oldCap = cap;
                    cap *= 2;
                    buf = (char*)reallocate(buf, oldCap, cap);
                }
                if (n < cap - len) break;
            }
            if (len == 0 && feof(f->file)) {
                reallocate(buf, cap, 0);
                *result = NIL_VAL;
                return;
            }
            Value s = OBJ_VAL(copyString(buf, (int)len));
            reallocate(buf, cap, 0);
            *result = s;
            return;
        }
        if (strcmp(m, "*l") == 0 || strcmp(m, "*line") == 0 || strcmp(m, "l") == 0) {
            size_t cap = 256;
            size_t len = 0;
            char* buf = (char*)reallocate(NULL, 0, cap);
            int c;
            while ((c = fgetc(f->file)) != EOF) {
                if (c == '\n') break;
                if (len + 1 >= cap) {
                    size_t oldCap = cap;
                    cap *= 2;
                    buf = (char*)reallocate(buf, oldCap, cap);
                }
                buf[len++] = (char)c;
            }
            if (len == 0 && c == EOF) {
                reallocate(buf, cap, 0);
                *result = NIL_VAL;
                return;
            }
            if (len > 0 && buf[len-1] == '\r') len--;
            Value s = OBJ_VAL(copyString(buf, (int)len));
            reallocate(buf, cap, 0);
            *result = s;
            return;
        }
    }
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)reallocate(NULL, 0, cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f->file)) > 0) {
        len += n;
        if (len == cap) {
            size_t oldCap = cap;
            cap *= 2;
            buf = (char*)reallocate(buf, oldCap, cap);
        }
    }
    Value s = OBJ_VAL(copyString(buf, (int)len));
    reallocate(buf, cap, 0);
    *result = s;
}

static void ioWrite(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !isFileValue(args[0])) { *result = NIL_VAL; return; }
    ObjFile* f = AS_FILE(args[0]);
    if (f->closed || !f->file) { *result = NIL_VAL; return; }
    for (int i = 1; i < argCount; i++) {
        if (!IS_STRING(args[i])) {
            continue;
        }
        ObjString* s = AS_STRING(args[i]);
        size_t written = fwrite(s->chars, 1, s->length, f->file);
        if (written != (size_t)s->length) { *result = NIL_VAL; return; }
    }
    *result = OBJ_VAL(f);
}

static void ioExists(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = FALSE_VAL; return; }
    ObjString* path = AS_STRING(args[0]);
    struct stat st;
    int r = stat(path->chars, &st);
    *result = BOOL_VAL(r == 0);
}

static void ioRemove(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = FALSE_VAL; return; }
    ObjString* path = AS_STRING(args[0]);
    int r = remove(path->chars);
    *result = BOOL_VAL(r == 0);
}

static void ioRename(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) { *result = FALSE_VAL; return; }
    const char* oldp = AS_STRING(args[0])->chars;
    const char* newp = AS_STRING(args[1])->chars;
    int r = rename(oldp, newp);
    *result = BOOL_VAL(r == 0);
}

static void ioMkdir(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = FALSE_VAL; return; }
    const char* path = AS_STRING(args[0])->chars;
#ifdef _WIN32
    int r = _mkdir(path);
#else
    int r = mkdir(path, 0755);
#endif
    *result = BOOL_VAL(r == 0);
}

static void ioList(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = NIL_VAL; return; }
    const char* path = AS_STRING(args[0])->chars;
    ObjTable* tbl = newTable();
    int idx = 1;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { *result = OBJ_VAL(tbl); return; }
    do {
        const char* name = fd.cFileName;
        if (strcmp(name, ".")==0 || strcmp(name, "..")==0) continue;
        Value key = INTEGER_VAL(idx++);
        Value val = OBJ_VAL(copyString(name, (int)strlen(name)));

        tableSet(&tbl->fields, key, val);
        if (idx-2 < tbl->arrayCapacity) {
        }
        if (tbl->arrayCapacity < idx) {
            int newCap = tbl->arrayCapacity * 2 + 4;
            if (newCap < idx) newCap = idx;
            tbl->array = (Value*)reallocate(tbl->array, sizeof(Value)*tbl->arrayCapacity, sizeof(Value)*newCap);
            for(int i=tbl->arrayCapacity;i<newCap;i++) tbl->array[i]=NIL_VAL;
            tbl->arrayCapacity = newCap;
        }
        tbl->array[idx-2] = val;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path);
    if (!d) { *result = OBJ_VAL(tbl); return; }
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0) continue;
        Value val = OBJ_VAL(copyString(e->d_name, (int)strlen(e->d_name)));
        if (tbl->arrayCapacity < idx) {
            int newCap = tbl->arrayCapacity * 2 + 4;
            if (newCap < idx) newCap = idx;
            tbl->array = (Value*)reallocate(tbl->array, sizeof(Value)*tbl->arrayCapacity, sizeof(Value)*newCap);
            for(int i=tbl->arrayCapacity;i<newCap;i++) tbl->array[i]=NIL_VAL;
            tbl->arrayCapacity = newCap;
        }
        tbl->array[idx-1] = val;
        tableSet(&tbl->fields, INTEGER_VAL(idx), val);
        idx++;
    }
    closedir(d);
#endif
    *result = OBJ_VAL(tbl);
}

static void ioReadFile(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_STRING(args[0])) { *result = NIL_VAL; return; }
    const char* path = AS_STRING(args[0])->chars;
    FILE* f = fopen(path, "rb");
    if (!f) { *result = NIL_VAL; return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) sz = 0;
    char* buf = (char*)reallocate(NULL, 0, sz + 1);
    size_t read = 0;
    if (sz > 0) read = fread(buf, 1, sz, f);
    fclose(f);
    Value s = OBJ_VAL(copyString(buf, (int)read));
    reallocate(buf, sz+1, 0);
    *result = s;
}

static void ioWriteFile(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) { *result = FALSE_VAL; return; }
    const char* path = AS_STRING(args[0])->chars;
    ObjString* data = AS_STRING(args[1]);
    FILE* f = fopen(path, "wb");
    if (!f) { *result = FALSE_VAL; return; }
    size_t written = fwrite(data->chars, 1, data->length, f);
    fclose(f);
    *result = BOOL_VAL(written == (size_t)data->length);
}

void initIOLibrary(void) {
    ObjTable* io = newTable();
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("io",2)), OBJ_VAL(io));

    #define ADD_IO(name, fn, arity) do { \
        ObjNative* nat = newNative(name, fn, arity); \
        Value key = OBJ_VAL((Obj*)copyString(name, strlen(name))); \
        tableSet(&io->fields, key, OBJ_VAL(nat)); \
        tableSet(&vm.globals, key, OBJ_VAL(nat)); \
    } while(0)

    ObjNative* openNat = newNative("open", ioOpen, -1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("open",4)), OBJ_VAL(openNat));
    ObjNative* closeNat = newNative("close", ioClose, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("close",5)), OBJ_VAL(closeNat));
    ObjNative* readNat = newNative("read", ioRead, -1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("read",4)), OBJ_VAL(readNat));
    ObjNative* writeNat = newNative("write", ioWrite, -1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("write",5)), OBJ_VAL(writeNat));
    ObjNative* flushNat = newNative("flush", ioFlush, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("flush",5)), OBJ_VAL(flushNat));
    ObjNative* seekNat = newNative("seek", ioSeek, -1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("seek",4)), OBJ_VAL(seekNat));
    ObjNative* existsNat = newNative("exists", ioExists, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("exists",6)), OBJ_VAL(existsNat));
    ObjNative* removeNat = newNative("remove", ioRemove, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("remove",6)), OBJ_VAL(removeNat));
    ObjNative* renameNat = newNative("rename", ioRename, 2);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("rename",6)), OBJ_VAL(renameNat));
    ObjNative* mkdirNat = newNative("mkdir", ioMkdir, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("mkdir",5)), OBJ_VAL(mkdirNat));
    ObjNative* listNat = newNative("list", ioList, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("list",4)), OBJ_VAL(listNat));
    ObjNative* readFileNat = newNative("readFile", ioReadFile, 1);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("readFile",8)), OBJ_VAL(readFileNat));
    ObjNative* writeFileNat = newNative("writeFile", ioWriteFile, 2);
    tableSet(&io->fields, OBJ_VAL((Obj*)copyString("writeFile",9)), OBJ_VAL(writeFileNat));

    ObjTable* fileProto = newTable();
    vm.fileProto = fileProto;
    tableSet(&fileProto->fields, OBJ_VAL((Obj*)copyString("read",4)), OBJ_VAL(readNat));
    tableSet(&fileProto->fields, OBJ_VAL((Obj*)copyString("write",5)), OBJ_VAL(writeNat));
    tableSet(&fileProto->fields, OBJ_VAL((Obj*)copyString("close",5)), OBJ_VAL(closeNat));
    tableSet(&fileProto->fields, OBJ_VAL((Obj*)copyString("flush",5)), OBJ_VAL(flushNat));
    tableSet(&fileProto->fields, OBJ_VAL((Obj*)copyString("seek",4)), OBJ_VAL(seekNat));

    #undef ADD_IO
}
