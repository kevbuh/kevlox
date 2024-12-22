#include <stdio.h>
#include <stdarg.h>

#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm; // TODO: don't make this global

static void resetStack() {
    vm.stackTop = vm.stack; //  point to the beginning of the array
}

// tell the user which line of their code was being executed when the error occurred
static void runtimeError(const char* format, ...) {
    // uses variadic functions -- take a varying number of arguments
    va_list args; // lets us pass an abitrary number of arguments to runtimeError()
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);
}

void initVM() {
    resetStack();
}

static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

// nil and false are false, every other behaves as true
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run() {
    #define READ_BYTE() (*vm.ip++)
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
    // + - * / args must be numbers
    // if they aren't, throw runtime error
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
                runtimeError("Operands must be numbers."); \
                return INTERPRET_RUNTIME_ERROR; \
            }\
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } while (false)

    for (;;) {
        #ifdef DEBUG_TRACE_EXECUTION
        printf("s        ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[");
            printValue(*slot);
            printf("]");
        }
        printf("\n");
        disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
        #endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_ADD: BINARY_OP(NUMBER_VAL, +); break;
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
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
    #undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk)) { // compiler take the user’s program and fill up the chunk with bytecode
      freeChunk(&chunk);
      return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk; // send chunk to VM to be executed
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}

void freeVM() {}

// push onto top of stack
void push(Value value) {
    *vm.stackTop = value; // store value at top of the stack (currently empty)
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}