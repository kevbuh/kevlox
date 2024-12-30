#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"
 
typedef enum {
    OP_CONSTANT,       // 2 bytes: [opcode, constant index] - Loads a constant value onto the stack.
    OP_NIL,            // 1 byte: Pushes a `nil` value onto the stack.
    OP_TRUE,           // 1 byte: Pushes a `true` value onto the stack.
    OP_FALSE,          // 1 byte: Pushes a `false` value onto the stack.
    OP_POP,            // 1 byte: Pops the top value off the stack.
    OP_GET_LOCAL,      // 2 bytes: [opcode, local slot index] - Loads a local variable onto the stack.
    OP_SET_LOCAL,      // 2 bytes: [opcode, local slot index] - Stores the top stack value in a local variable.
    OP_GET_GLOBAL,     // 2 bytes: [opcode, constant index] - Loads a global variable onto the stack.
    OP_DEFINE_GLOBAL,  // 2 bytes: [opcode, constant index] - Defines a global variable with the top stack value.
    OP_SET_GLOBAL,     // 2 bytes: [opcode, constant index] - Stores the top stack value in a global variable.
    OP_EQUAL,          // 1 byte: Pops two values, compares them for equality, and pushes the result.
    OP_GREATER,        // 1 byte: Pops two values, checks if the first is greater than the second, and pushes the result.
    OP_LESS,           // 1 byte: Pops two values, checks if the first is less than the second, and pushes the result.
    OP_ADD,            // 1 byte: Pops two values, adds them, and pushes the result.
    OP_SUBTRACT,       // 1 byte: Pops two values, subtracts the second from the first, and pushes the result.
    OP_MULTIPLY,       // 1 byte: Pops two values, multiplies them, and pushes the result.
    OP_DIVIDE,         // 1 byte: Pops two values, divides the first by the second, and pushes the result.
    OP_NOT,            // 1 byte: Pops a value, negates it (logical NOT), and pushes the result.
    OP_NEGATE,         // 2 bytes: [opcode, value to negate] - Pops a value, negates it (arithmetic negation), and pushes the result.
    OP_PRINT,          // 1 byte: Pops a value and prints it.
    OP_JUMP,           // 3 bytes: [opcode, jump offset] - Unconditionally jumps to a new instruction offset.
    OP_JUMP_IF_FALSE,  // 3 bytes: [opcode, jump offset] - Jumps to a new instruction offset if the top stack value is false.
    OP_LOOP,           // 3 bytes: [opcode, loop offset] - Jumps backward by a specified offset (used for loops).
    OP_CALL,           // 2 bytes: [opcode, argument count] - Calls a function with the specified number of arguments.
    OP_RETURN,         // 1 byte: Returns from the current function, optionally returning a value.
} OpCode;

typedef struct {
    int count; // current number of bytes of bytecode in the code array.
    int capacity; // total allocated capacity of the code array
    uint8_t* code; // array that holds the bytecode instructions
    int* lines; // maps each byte of bytecode in code to its corresponding source code line number
    ValueArray constants; // array of constants used by the bytecode in this chunk
} Chunk; // Bytecode is a series of instructions

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif