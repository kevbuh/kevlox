#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    advance(); // prime the pump
    expression(); // parse single expression
    consume(TOKEN_EOF, "expect end of expression"); // check for EOF token
}
