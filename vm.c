#include <stdio.h>
#include "common.h"
#include "vm.h"

VM vm; // TODO: don't make this global

void initVM() {

}

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    for (;;) {
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                printValue(constant);
                printf("\n");
                break;
            }
            case OP_RETURN: {
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