#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
 
typedef enum {
    OP_RETURN, // return from the current function
} OpCode;

typedef struct {
    int count;
    int capacity;
    uint8_t* code; // array of bytes
} Chunk; // Bytecode is a series of instructions

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte);

#endif

