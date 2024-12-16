#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef double Value;

// constant pool: dynamic array of Values
// wraps a pointer to an array along with its allocated capacity and the number of elements in use
typedef struct {
    int capacity; 
    int count; 
    Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif