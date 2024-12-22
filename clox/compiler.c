#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode; // to stop cascading error messages
} Parser;

// Lox's precedence levels from lowest to highest
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;

static Chunk* currentChunk() {
    return compilingChunk;
}

// print where the error occured
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] ERROR", token->line); 

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current; // stash current token so we can reference it after a match

    for (;;) { // step forward through the token stream
        parser.current = scanToken(); // ask sanner for next token and store it
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

// reads the next token and validates that the token has an expected type
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

// writes the given byte, which may be an opcode or an operand to an instruction
// sends in the previous token’s line information so that runtime errors are associated with that line
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk");
        return 0;
    }
    return (uint8_t)constant;
}

// first add the value to the constant table
// then emit an OP_CONSTANT instruction that pushes it onto the stack at runtime
static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void endCompiler() {
    emitReturn();
    #ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
    #endif
}

// forward declare
static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary () {
    // remember the operator
    TokenType operatorType = parser.previous.type;

    // compile the right operand
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    // emit bytecode of the operator instruction
    switch (operatorType) {
        case TOKEN_PLUS:  emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:  emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        default:
            return;
    }
}

static void literal() {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default:
            return; // unreachable
    }
}

static void grouping() {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void number() {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void unary() {
    TokenType operatorType = parser.previous.type;

    // compile the operand
    parsePrecedence(PREC_UNARY); // own PREC_UNARY precedence to permit nested unary expressions like !!d

    // emit the operator instruction
    switch(operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default:
            return; // unreachable
    }
}

// {prefix fn, infix fn, infix precedence}
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL, PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL, NULL, PREC_NONE},  
  [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
  [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
  [TOKEN_DOT]           = {NULL, NULL, PREC_NONE},
  [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
  [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL, NULL, PREC_NONE},
  [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL, NULL, PREC_NONE},
  [TOKEN_GREATER]       = {NULL, NULL, PREC_NONE},
  [TOKEN_GREATER_EQUAL] = {NULL, NULL, PREC_NONE},
  [TOKEN_LESS]          = {NULL, NULL, PREC_NONE},
  [TOKEN_LESS_EQUAL]    = {NULL, NULL, PREC_NONE},
  [TOKEN_IDENTIFIER]    = {NULL, NULL, PREC_NONE},
  [TOKEN_STRING]        = {NULL, NULL, PREC_NONE},
  [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
  [TOKEN_AND]           = {NULL, NULL, PREC_NONE},
  [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
  [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
  [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
  [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
  [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
  [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
  [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
  [TOKEN_OR]            = {NULL, NULL, PREC_NONE},
  [TOKEN_PRINT]         = {NULL, NULL, PREC_NONE},
  [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
  [TOKEN_SUPER]         = {NULL, NULL, PREC_NONE},
  [TOKEN_THIS]          = {NULL, NULL, PREC_NONE},
  [TOKEN_TRUE]          = {literal, NULL, PREC_NONE},
  [TOKEN_VAR]           = {NULL, NULL, PREC_NONE},
  [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
  [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
  [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

// parse given precedence
// starts at the current token and parses any expression at the given precedence level or higher
static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expected expression");
        return;
    }

    prefixRule();

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

// returs rule from parse fn table
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT); // parse the lowest precedence level
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    // reset from previous compile
    parser.hadError = false;
    parser.panicMode = false;

    advance(); // prime the pump
    expression(); // parse single expression
    consume(TOKEN_EOF, "Expected end of expression"); // check for EOF token
    endCompiler();
    return !parser.hadError;
}