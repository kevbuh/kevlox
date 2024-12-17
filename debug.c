#include <stdio.h>
#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("CNK  LN  OP ===%s=== I VAL\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

static int simpleInstruction(const char* name, int offset) {
    printf(" %s\n", name);
    return offset + 1; // return next byte
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset+1];
    printf("%-16s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2; // OP_CONSTANT is two bytes: opcode and operand
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d", offset); // prints byte offset of the given instruction

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset-1]) {
        printf("   ^");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset]; // opcode
    switch(instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default: 
            printf("Unknown opcode %d \n", instruction);
            return offset + 1;
    }
}