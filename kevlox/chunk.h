#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"
 
typedef enum {
    OP_CONSTANT,       // 2 bytes: [opcode, constant index]   - loads a constant value onto the stack
    OP_NIL,            // 1 byte:                             - pushes a `nil` value onto the stack
    OP_TRUE,           // 1 byte:                             - pushes a `true` value onto the stack
    OP_FALSE,          // 1 byte:                             - pushes a `false` value onto the stack
    OP_POP,            // 1 byte:                             - pops the top value off the stack
    OP_GET_LOCAL,      // 2 bytes: [opcode, local slot index] - loads a local variable onto the stack
    OP_SET_LOCAL,      // 2 bytes: [opcode, local slot index] - stores the top stack value in a local variable
    OP_GET_GLOBAL,     // 2 bytes: [opcode, constant index]   - loads a global variable onto the stack
    OP_DEFINE_GLOBAL,  // 2 bytes: [opcode, constant index]   - defines a global variable with the top stack value
    OP_SET_GLOBAL,     // 2 bytes: [opcode, constant index]   - stores the top stack value in a global variable
    OP_GET_UPVALUE,    // 2 bytes: [opcode, upvalue index]    - loads an upvalue (captured variable) onto the stack
    OP_SET_UPVALUE,    // 2 bytes: [opcode, upvalue index]    - stores the top stack value in an upvalue
    OP_EQUAL,          // 1 byte:                             - pops two values, compares them for equality, and pushes the result
    OP_GREATER,        // 1 byte:                             - pops two values, checks if the first is greater than the second, and pushes the result
    OP_LESS,           // 1 byte:                             - pops two values, checks if the first is less than the second, and pushes the result
    OP_ADD,            // 1 byte:                             - pops two values, adds them, and pushes the result
    OP_SUBTRACT,       // 1 byte:                             - pops two values, subtracts the second from the first, and pushes the result
    OP_MULTIPLY,       // 1 byte:                             - pops two values, multiplies them, and pushes the result
    OP_DIVIDE,         // 1 byte:                             - pops two values, divides the first by the second, and pushes the result
    OP_NOT,            // 1 byte:                             - pops a value, negates it (logical NOT), and pushes the result
    OP_NEGATE,         // 2 bytes: [opcode, value to negate]  - pops a value, negates it (arithmetic negation), and pushes the result
    OP_PRINT,          // 1 byte:                             - pops a value and prints it
    OP_JUMP,           // 3 bytes: [opcode, jump offset]      - unconditionally jumps to a new instruction offset
    OP_JUMP_IF_FALSE,  // 3 bytes: [opcode, jump offset]      - jumps to a new instruction offset if the top stack value is false
    OP_LOOP,           // 3 bytes: [opcode, loop offset]      - jumps backward by a specified offset (used for loops)
    OP_CALL,           // 2 bytes: [opcode, argument count]   - calls a function with the specified number of arguments
    OP_CLOSURE,        // Variable bytes: [opcode, function index, upvalue count, upvalue indices] - creates a closure for a function
    OP_CLOSE_UPVALUE,
    OP_RETURN,         // 1 byte:                             - returns from the current function, optionally returning a value
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