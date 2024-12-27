#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"
 
typedef enum {
    OP_CONSTANT, // 00: 2 bytes, [opcode, constant index]
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,   // 00: 2 bytes, [opcode, val to negate]
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_RETURN,   // 01: 1 byte opcode
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