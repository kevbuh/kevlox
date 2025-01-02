#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ, // lives on the heap
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
    } as;
} Value;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean) // checks value as bool
#define AS_NUMBER(value)  ((value).as.number)  // checks value as number
#define AS_OBJ(value)     ((value).as.obj)

// creates a Value object of type VAL_BOOL and assigns the value to the boolean member of the union
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})

// creates a Value object of type VAL_NIL. Since VAL_NIL does not require any meaningful data, the .number member is simply initialized to 0
#define NIL_VAL           ((Value){VAL_NIL,  {.number = 0}})

// creates a Value object of type VAL_NUMBER and assigns the value to the number member of the union
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

// takes a bare Obj pointer and wraps it in a full Value
#define OBJ_VAL(object)    ((Value){VAL_OBJ, {.obj = (Obj*)object}})

// typedef double Value;

// constant pool: dynamic array of Values
// wraps a pointer to an array along with its allocated capacity and the number of elements in use
typedef struct {
    int capacity; 
    int count; 
    Value* values;
} ValueArray;

bool valuesEqual(Value a, Value b);

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif