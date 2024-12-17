#include <stdio.h>
#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm; // TODO: don't make this global

static void resetStack() {
    vm.stackTop = vm.stack; //  point to the beginning of the array
}

void initVM() {
    resetStack();
}

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    for (;;) {
        #ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
        #endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                // printf("RUN: ");
                // printValue(constant);
                // printf("\n");
                break;
            }
            case OP_NEGATE: 
                push(-pop());
                break;
            case OP_RETURN: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }
        }
    }

    #undef READ_BYTE
    #undef READ_CONSTANT
}

InterpretResult interpret(Chunk* chunk) {
    vm.chunk = chunk;

    // TODO: store as local variable so its in a register
    // first byte of code in the chunk
    vm.ip = vm.chunk->code;
    return run();
}

void freeVM() {

}

void push(Value value) {
    *vm.stackTop = value; // store value at top of the stack (currently empty)
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}