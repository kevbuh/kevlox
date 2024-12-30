#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "chunk.h"
#include "value.h"
#include "compiler.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// represents a single ongoing function call
typedef struct {
    ObjFunction* function;
    uint8_t* ip;
    Value* slots; // points to the VM’s value stack at the first slot that this function can use
} CallFrame;

typedef struct {
    // each CallFrame has its own up and pointer tot eh ObjFunction that its executing
    CallFrame frames[FRAMES_MAX]; 
    int frameCount;

    Value stack[STACK_MAX]; // bytecode stack
    Value* stackTop; // points to where the next value to be pushed will go
    Table globals; // global variables
    Table strings; // string table for string interning
    Obj* objects; // point to head of list for garbage collection
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif
