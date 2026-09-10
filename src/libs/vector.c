#include <math.h>
#include <string.h>
#include <float.h>

#include "libs/vector.h"
#include "vm.h"
#include "object.h"
#include "table.h"
#include "memory.h"

static bool toNumber(Value v, double* out) {
    if (IS_INTEGER(v)) { *out = (double)AS_INTEGER(v); return true; }
    if (IS_NUMBER(v)) { *out = AS_NUMBER_NC(v); return true; }
    return false;
}

static void vec2New(int argCount, Value* args, Value* result) {
    double x = 0, y = 0;
    if (argCount >= 1 && !IS_NIL(args[0])) {
        if (!toNumber(args[0], &x)) { *result = NIL_VAL; return; }
    }
    if (argCount >= 2 && !IS_NIL(args[1])) {
        if (!toNumber(args[1], &y)) { *result = NIL_VAL; return; }
    }
    ObjVector2* v = newVector2((float)x, (float)y);
    *result = OBJ_VAL(v);
}

static void vec3New(int argCount, Value* args, Value* result) {
    double x = 0, y = 0, z = 0;
    if (argCount >= 1 && !IS_NIL(args[0])) {
        if (!toNumber(args[0], &x)) { *result = NIL_VAL; return; }
    }
    if (argCount >= 2 && !IS_NIL(args[1])) {
        if (!toNumber(args[1], &y)) { *result = NIL_VAL; return; }
    }
    if (argCount >= 3 && !IS_NIL(args[2])) {
        if (!toNumber(args[2], &z)) { *result = NIL_VAL; return; }
    }
    ObjVector3* v = newVector3((float)x, (float)y, (float)z);
    *result = OBJ_VAL(v);
}

static void vec2Magnitude(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR2(args[0])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]);
    double m = sqrt((double)v->x * v->x + (double)v->y * v->y);
    *result = NUMBER_VAL(m);
}
static void vec2Unit(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR2(args[0])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]);
    double m = sqrt((double)v->x * v->x + (double)v->y * v->y);
    if (m == 0) {
        float nan = (float)NAN;
        *result = OBJ_VAL(newVector2(nan, nan));
        return;
    }
    *result = OBJ_VAL(newVector2((float)(v->x / m), (float)(v->y / m)));
}
static void vec2Dot(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = NIL_VAL; return; }
    ObjVector2* a = AS_VECTOR2(args[0]);
    ObjVector2* b = AS_VECTOR2(args[1]);
    *result = NUMBER_VAL((double)a->x * b->x + (double)a->y * b->y);
}
static void vec2Cross(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = NIL_VAL; return; }
    ObjVector2* a = AS_VECTOR2(args[0]);
    ObjVector2* b = AS_VECTOR2(args[1]);
    *result = NUMBER_VAL((double)a->x * b->y - (double)a->y * b->x);
}
static void vec2Lerp(int argCount, Value* args, Value* result) {
    if (argCount < 3 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = NIL_VAL; return; }
    double alpha; if (!toNumber(args[2], &alpha)) { *result = NIL_VAL; return; }
    ObjVector2* a = AS_VECTOR2(args[0]);
    ObjVector2* b = AS_VECTOR2(args[1]);
    float x = (float)(a->x + (b->x - a->x) * alpha);
    float y = (float)(a->y + (b->y - a->y) * alpha);
    *result = OBJ_VAL(newVector2(x, y));
}
static void vec2Angle(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = NIL_VAL; return; }
    ObjVector2* a = AS_VECTOR2(args[0]);
    ObjVector2* b = AS_VECTOR2(args[1]);
    double dot = (double)a->x * b->x + (double)a->y * b->y;
    double ma = sqrt((double)a->x * a->x + (double)a->y * a->y);
    double mb = sqrt((double)b->x * b->x + (double)b->y * b->y);
    if (ma == 0 || mb == 0) { *result = NUMBER_VAL(0); return; }
    double c = dot / (ma * mb);
    if (c > 1) c = 1;
    if (c < -1) c = -1;
    double ang = acos(c);
    if (argCount >= 3 && IS_BOOL(args[2]) && AS_BOOL(args[2])) {
        double cross = (double)a->x * b->y - (double)a->y * b->x;
        if (cross < 0) ang = -ang;
    }
    *result = NUMBER_VAL(ang);
}
static void vec2Max(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = NIL_VAL; return; }
    ObjVector2* a = AS_VECTOR2(args[0]); ObjVector2* b = AS_VECTOR2(args[1]);
    float x = a->x > b->x ? a->x : b->x;
    float y = a->y > b->y ? a->y : b->y;
    if (argCount > 2) {
        for (int i=2;i<argCount;i++) {
            if (!IS_VECTOR2(args[i])) continue;
            ObjVector2* v = AS_VECTOR2(args[i]);
            if (v->x > x) x = v->x;
            if (v->y > y) y = v->y;
        }
    }
    *result = OBJ_VAL(newVector2(x,y));
}
static void vec2Min(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = NIL_VAL; return; }
    ObjVector2* a = AS_VECTOR2(args[0]); ObjVector2* b = AS_VECTOR2(args[1]);
    float x = a->x < b->x ? a->x : b->x;
    float y = a->y < b->y ? a->y : b->y;
    for (int i=2;i<argCount;i++) {
        if (!IS_VECTOR2(args[i])) continue;
        ObjVector2* v = AS_VECTOR2(args[i]);
        if (v->x < x) x = v->x;
        if (v->y < y) y = v->y;
    }
    *result = OBJ_VAL(newVector2(x,y));
}
static void vec2Clamp(int argCount, Value* args, Value* result) {
    if (argCount < 3 || !IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1]) || !IS_VECTOR2(args[2])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]); ObjVector2* lo = AS_VECTOR2(args[1]); ObjVector2* hi = AS_VECTOR2(args[2]);
    float x = v->x < lo->x ? lo->x : (v->x > hi->x ? hi->x : v->x);
    float y = v->y < lo->y ? lo->y : (v->y > hi->y ? hi->y : v->y);
    *result = OBJ_VAL(newVector2(x,y));
}
static void vec2Floor(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR2(args[0])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]);
    *result = OBJ_VAL(newVector2(floorf(v->x), floorf(v->y)));
}
static void vec2Ceil(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR2(args[0])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]);
    *result = OBJ_VAL(newVector2(ceilf(v->x), ceilf(v->y)));
}
static void vec2Abs(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR2(args[0])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]);
    *result = OBJ_VAL(newVector2(fabsf(v->x), fabsf(v->y)));
}
static void vec2Sign(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR2(args[0])) { *result = NIL_VAL; return; }
    ObjVector2* v = AS_VECTOR2(args[0]);
    float sx = (v->x > 0) ? 1 : (v->x < 0 ? -1 : 0);
    float sy = (v->y > 0) ? 1 : (v->y < 0 ? -1 : 0);
    *result = OBJ_VAL(newVector2(sx, sy));
}

static void vec3Magnitude(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR3(args[0])) { *result = NIL_VAL; return; }
    ObjVector3* v = AS_VECTOR3(args[0]);
    double m = sqrt((double)v->x*v->x + (double)v->y*v->y + (double)v->z*v->z);
    *result = NUMBER_VAL(m);
}
static void vec3Unit(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR3(args[0])) { *result = NIL_VAL; return; }
    ObjVector3* v = AS_VECTOR3(args[0]);
    double m = sqrt((double)v->x*v->x + (double)v->y*v->y + (double)v->z*v->z);
    if (m == 0) {
        float nan = (float)NAN;
        *result = OBJ_VAL(newVector3(nan,nan,nan));
        return;
    }
    *result = OBJ_VAL(newVector3((float)(v->x/m), (float)(v->y/m), (float)(v->z/m)));
}
static void vec3Dot(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = NIL_VAL; return; }
    ObjVector3* a = AS_VECTOR3(args[0]); ObjVector3* b = AS_VECTOR3(args[1]);
    *result = NUMBER_VAL((double)a->x*b->x + (double)a->y*b->y + (double)a->z*b->z);
}
static void vec3Cross(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = NIL_VAL; return; }
    ObjVector3* a = AS_VECTOR3(args[0]); ObjVector3* b = AS_VECTOR3(args[1]);
    float x = a->y*b->z - a->z*b->y;
    float y = a->z*b->x - a->x*b->z;
    float z = a->x*b->y - a->y*b->x;
    *result = OBJ_VAL(newVector3(x,y,z));
}
static void vec3Lerp(int argCount, Value* args, Value* result) {
    if (argCount < 3 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = NIL_VAL; return; }
    double alpha; if (!toNumber(args[2], &alpha)) { *result = NIL_VAL; return; }
    ObjVector3* a = AS_VECTOR3(args[0]); ObjVector3* b = AS_VECTOR3(args[1]);
    float x = (float)(a->x + (b->x - a->x)*alpha);
    float y = (float)(a->y + (b->y - a->y)*alpha);
    float z = (float)(a->z + (b->z - a->z)*alpha);
    *result = OBJ_VAL(newVector3(x,y,z));
}
static void vec3Angle(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = NIL_VAL; return; }
    ObjVector3* a = AS_VECTOR3(args[0]); ObjVector3* b = AS_VECTOR3(args[1]);
    double dot = (double)a->x*b->x + (double)a->y*b->y + (double)a->z*b->z;
    double ma = sqrt((double)a->x*a->x + (double)a->y*a->y + (double)a->z*a->z);
    double mb = sqrt((double)b->x*b->x + (double)b->y*b->y + (double)b->z*b->z);
    if (ma==0||mb==0){ *result=NUMBER_VAL(0); return; }
    double c = dot/(ma*mb);
    if(c>1) c=1;
    if(c<-1) c=-1;
    double ang = acos(c);
    if (argCount>=3 && IS_VECTOR3(args[2])) {
        ObjVector3* axis = AS_VECTOR3(args[2]);
        double cx = a->y*b->z - a->z*b->y;
        double cy = a->z*b->x - a->x*b->z;
        double cz = a->x*b->y - a->y*b->x;
        double sign = axis->x*cx + axis->y*cy + axis->z*cz;
        if (sign < 0) ang = -ang;
    }
    *result = NUMBER_VAL(ang);
}
static void vec3Max(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = NIL_VAL; return; }
    ObjVector3* a = AS_VECTOR3(args[0]); ObjVector3* b = AS_VECTOR3(args[1]);
    float x = a->x > b->x ? a->x : b->x;
    float y = a->y > b->y ? a->y : b->y;
    float z = a->z > b->z ? a->z : b->z;
    for(int i=2;i<argCount;i++) if(IS_VECTOR3(args[i])){ ObjVector3* v=AS_VECTOR3(args[i]); if(v->x>x)x=v->x; if(v->y>y)y=v->y; if(v->z>z)z=v->z; }
    *result = OBJ_VAL(newVector3(x,y,z));
}
static void vec3Min(int argCount, Value* args, Value* result) {
    if (argCount < 2 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = NIL_VAL; return; }
    ObjVector3* a = AS_VECTOR3(args[0]); ObjVector3* b = AS_VECTOR3(args[1]);
    float x = a->x < b->x ? a->x : b->x;
    float y = a->y < b->y ? a->y : b->y;
    float z = a->z < b->z ? a->z : b->z;
    for(int i=2;i<argCount;i++) if(IS_VECTOR3(args[i])){ ObjVector3* v=AS_VECTOR3(args[i]); if(v->x<x)x=v->x; if(v->y<y)y=v->y; if(v->z<z)z=v->z; }
    *result = OBJ_VAL(newVector3(x,y,z));
}
static void vec3Clamp(int argCount, Value* args, Value* result) {
    if (argCount < 3 || !IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1]) || !IS_VECTOR3(args[2])) { *result = NIL_VAL; return; }
    ObjVector3* v=AS_VECTOR3(args[0]); ObjVector3* lo=AS_VECTOR3(args[1]); ObjVector3* hi=AS_VECTOR3(args[2]);
    float x = v->x < lo->x ? lo->x : (v->x > hi->x ? hi->x : v->x);
    float y = v->y < lo->y ? lo->y : (v->y > hi->y ? hi->y : v->y);
    float z = v->z < lo->z ? lo->z : (v->z > hi->z ? hi->z : v->z);
    *result = OBJ_VAL(newVector3(x,y,z));
}
static void vec3Floor(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR3(args[0])) { *result = NIL_VAL; return; }
    ObjVector3* v=AS_VECTOR3(args[0]); *result=OBJ_VAL(newVector3(floorf(v->x),floorf(v->y),floorf(v->z)));
}
static void vec3Ceil(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR3(args[0])) { *result = NIL_VAL; return; }
    ObjVector3* v=AS_VECTOR3(args[0]); *result=OBJ_VAL(newVector3(ceilf(v->x),ceilf(v->y),ceilf(v->z)));
}
static void vec3Abs(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR3(args[0])) { *result = NIL_VAL; return; }
    ObjVector3* v=AS_VECTOR3(args[0]); *result=OBJ_VAL(newVector3(fabsf(v->x),fabsf(v->y),fabsf(v->z)));
}
static void vec3Sign(int argCount, Value* args, Value* result) {
    if (argCount < 1 || !IS_VECTOR3(args[0])) { *result = NIL_VAL; return; }
    ObjVector3* v=AS_VECTOR3(args[0]);
    float sx=(v->x>0)?1:(v->x<0?-1:0);
    float sy=(v->y>0)?1:(v->y<0?-1:0);
    float sz=(v->z>0)?1:(v->z<0?-1:0);
    *result=OBJ_VAL(newVector3(sx,sy,sz));
}
static void vec3FuzzyEq(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = FALSE_VAL; return; }
    if (!IS_VECTOR3(args[0]) || !IS_VECTOR3(args[1])) { *result = FALSE_VAL; return; }
    double eps = 1e-6; if (argCount>=3) toNumber(args[2], &eps);
    ObjVector3* a=AS_VECTOR3(args[0]); ObjVector3* b=AS_VECTOR3(args[1]);
    bool eq = fabsf(a->x-b->x) <= eps && fabsf(a->y-b->y) <= eps && fabsf(a->z-b->z) <= eps;
    *result = BOOL_VAL(eq);
}
static void vec2FuzzyEq(int argCount, Value* args, Value* result) {
    if (argCount < 2) { *result = FALSE_VAL; return; }
    if (!IS_VECTOR2(args[0]) || !IS_VECTOR2(args[1])) { *result = FALSE_VAL; return; }
    double eps = 1e-6; if (argCount>=3) toNumber(args[2], &eps);
    ObjVector2* a=AS_VECTOR2(args[0]); ObjVector2* b=AS_VECTOR2(args[1]);
    bool eq = fabsf(a->x-b->x) <= eps && fabsf(a->y-b->y) <= eps;
    *result = BOOL_VAL(eq);
}

static void vecTypeof(int argCount, Value* args, Value* result) {
    if (argCount < 1) { *result = NIL_VAL; return; }
    Value v = args[0];
    const char* t = NULL;
    if (IS_NIL(v)) t = "nil";
    else if (IS_BOOL(v)) t = "boolean";
    else if (IS_INTEGER(v) || IS_NUMBER(v)) t = "number";
    else if (IS_STRING(v)) t = "string";
    else if (IS_TABLE(v)) t = "table";
    else if (IS_CLOSURE(v) || IS_NATIVE(v) || IS_FUNCTION(v)) t = "function";
    else if (IS_VECTOR2(v)) t = "Vector2";
    else if (IS_VECTOR3(v)) t = "Vector3";
    else if (IS_ERR(v)) t = "error";
    else { *result = NIL_VAL; return; }
    *result = OBJ_VAL(copyString(t, (int)strlen(t)));
}

static void addToTable(ObjTable* tbl, const char* name, NativeFn fn, int arity) {
    ObjNative* nat = newNative(name, fn, arity);
    Value key = OBJ_VAL((Obj*)copyString(name, (int)strlen(name)));
    Value val = OBJ_VAL((Obj*)nat);
    tableSet(&tbl->fields, key, val);
}

void initVectorLibrary(void) {
    vm.vectorX = copyString("X",1);
    vm.vectorY = copyString("Y",1);
    vm.vectorZ = copyString("Z",1);
    vm.vectorMagnitude = copyString("Magnitude",9);
    vm.vectorUnit = copyString("Unit",4);
    vm.vectorDot = copyString("Dot",3);
    vm.vectorCross = copyString("Cross",5);
    vm.vectorLerp = copyString("Lerp",4);
    vm.vectorMagnitudeStr = copyString("magnitude",9);
    vm.vectorNormalize = copyString("normalize",9);

    ObjTable* vec2 = newTable();
    vm.vector2Table = vec2;
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("Vector2",7)), OBJ_VAL(vec2));
    addToTable(vec2, "new", vec2New, -1);
    addToTable(vec2, "Dot", vec2Dot, 2);
    addToTable(vec2, "dot", vec2Dot, 2);
    addToTable(vec2, "Cross", vec2Cross, 2);
    addToTable(vec2, "cross", vec2Cross, 2);
    addToTable(vec2, "Lerp", vec2Lerp, 3);
    addToTable(vec2, "lerp", vec2Lerp, 3);
    addToTable(vec2, "Magnitude", vec2Magnitude, 1);
    addToTable(vec2, "magnitude", vec2Magnitude, 1);
    addToTable(vec2, "Unit", vec2Unit, 1);
    addToTable(vec2, "normalize", vec2Unit, 1);
    addToTable(vec2, "Normalize", vec2Unit, 1);
    addToTable(vec2, "Angle", vec2Angle, -1);
    addToTable(vec2, "angle", vec2Angle, -1);
    addToTable(vec2, "Max", vec2Max, -1);
    addToTable(vec2, "max", vec2Max, -1);
    addToTable(vec2, "Min", vec2Min, -1);
    addToTable(vec2, "min", vec2Min, -1);
    addToTable(vec2, "Clamp", vec2Clamp, 3);
    addToTable(vec2, "clamp", vec2Clamp, 3);
    addToTable(vec2, "Floor", vec2Floor, 1);
    addToTable(vec2, "floor", vec2Floor, 1);
    addToTable(vec2, "Ceil", vec2Ceil, 1);
    addToTable(vec2, "ceil", vec2Ceil, 1);
    addToTable(vec2, "Abs", vec2Abs, 1);
    addToTable(vec2, "abs", vec2Abs, 1);
    addToTable(vec2, "Sign", vec2Sign, 1);
    addToTable(vec2, "sign", vec2Sign, 1);
    addToTable(vec2, "FuzzyEq", vec2FuzzyEq, -1);
    addToTable(vec2, "fuzzyEq", vec2FuzzyEq, -1);
    addToTable(vec2, "zero", vec2New, 0);

    ObjTable* vec3 = newTable();
    vm.vector3Table = vec3;
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("Vector3",7)), OBJ_VAL(vec3));
    addToTable(vec3, "new", vec3New, -1);
    addToTable(vec3, "Dot", vec3Dot, 2);
    addToTable(vec3, "dot", vec3Dot, 2);
    addToTable(vec3, "Cross", vec3Cross, 2);
    addToTable(vec3, "cross", vec3Cross, 2);
    addToTable(vec3, "Lerp", vec3Lerp, 3);
    addToTable(vec3, "lerp", vec3Lerp, 3);
    addToTable(vec3, "Magnitude", vec3Magnitude, 1);
    addToTable(vec3, "magnitude", vec3Magnitude, 1);
    addToTable(vec3, "Unit", vec3Unit, 1);
    addToTable(vec3, "normalize", vec3Unit, 1);
    addToTable(vec3, "Normalize", vec3Unit, 1);
    addToTable(vec3, "Angle", vec3Angle, -1);
    addToTable(vec3, "angle", vec3Angle, -1);
    addToTable(vec3, "Max", vec3Max, -1);
    addToTable(vec3, "max", vec3Max, -1);
    addToTable(vec3, "Min", vec3Min, -1);
    addToTable(vec3, "min", vec3Min, -1);
    addToTable(vec3, "Clamp", vec3Clamp, 3);
    addToTable(vec3, "clamp", vec3Clamp, 3);
    addToTable(vec3, "Floor", vec3Floor, 1);
    addToTable(vec3, "floor", vec3Floor, 1);
    addToTable(vec3, "Ceil", vec3Ceil, 1);
    addToTable(vec3, "ceil", vec3Ceil, 1);
    addToTable(vec3, "Abs", vec3Abs, 1);
    addToTable(vec3, "abs", vec3Abs, 1);
    addToTable(vec3, "Sign", vec3Sign, 1);
    addToTable(vec3, "sign", vec3Sign, 1);
    addToTable(vec3, "FuzzyEq", vec3FuzzyEq, -1);
    addToTable(vec3, "fuzzyEq", vec3FuzzyEq, -1);

    ObjTable* vecLib = newTable();
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("vector",6)), OBJ_VAL(vecLib));
    addToTable(vecLib, "create", vec3New, -1);
    addToTable(vecLib, "magnitude", vec3Magnitude, 1);
    addToTable(vecLib, "normalize", vec3Unit, 1);
    addToTable(vecLib, "cross", vec3Cross, 2);
    addToTable(vecLib, "dot", vec3Dot, 2);
    addToTable(vecLib, "angle", vec3Angle, -1);
    addToTable(vecLib, "floor", vec3Floor, 1);
    addToTable(vecLib, "ceil", vec3Ceil, 1);
    addToTable(vecLib, "abs", vec3Abs, 1);
    addToTable(vecLib, "sign", vec3Sign, 1);
    addToTable(vecLib, "clamp", vec3Clamp, 3);
    addToTable(vecLib, "max", vec3Max, -1);
    addToTable(vecLib, "min", vec3Min, -1);
    addToTable(vecLib, "lerp", vec3Lerp, 3);
    tableSet(&vecLib->fields, OBJ_VAL((Obj*)copyString("zero",4)), OBJ_VAL(newVector3(0,0,0)));
    tableSet(&vecLib->fields, OBJ_VAL((Obj*)copyString("one",3)), OBJ_VAL(newVector3(1,1,1)));
    tableSet(&vecLib->fields, OBJ_VAL((Obj*)copyString("zero2",5)), OBJ_VAL(newVector2(0,0)));
    tableSet(&vecLib->fields, OBJ_VAL((Obj*)copyString("one2",4)), OBJ_VAL(newVector2(1,1)));
    ObjNative* typeofNative = newNative("typeof", vecTypeof, 1);
    tableSet(&vm.globals, OBJ_VAL((Obj*)copyString("typeof",6)), OBJ_VAL((Obj*)typeofNative));
}
