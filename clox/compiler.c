#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name; // name of variable
    int depth; // depth of the block where the local variable was declared
} Local;

// simple flat array of all locals that are in scope during each point in the compilation process
typedef struct {
    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
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

// returns checks if current parser toekn has the given type
static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// writes the given byte to the chunk, which may be an opcode or an operand to an instruction
// also sends in the previous token’s line information so that runtime errors are associated with that line
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

static void initCompiler(Compiler* compiler) {
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
}

static void endCompiler() {
    emitReturn();
    #ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
    #endif
}

// adds scope depth
static void beginScope() {
    current->scopeDepth++;
}

// subtracts scope depth and removes locals from previous scope
static void endScope() {

    while(current->localCount > 0 && current->locals[current->localCount -1].depth > current->scopeDepth) {
        emitByte(OP_POP);
        current->localCount--;
    }

    current->scopeDepth--;
}

// forward declare
static void expression();
static void statement();
static void declaration();
static uint8_t identifierConstant(Token* name);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign) {
    // remember the operator
    TokenType operatorType = parser.previous.type;

    // compile the right operand
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    // emit bytecode of the operator instruction
    switch (operatorType) {
        case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS: emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:  emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:  emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        default:
            return;
    }
}

static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default:
            return; // unreachable
    }
}

static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
    // + trim lead  quotation mark
    // - trim trailing quotation mark
    // create a string object, wrap it ina value, stuffs it into the constant table
    emitConstant(OBJ_VAL(copyString(parser.previous.start+1, parser.previous.length-2)));
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t arg = identifierConstant(&name);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_GLOBAL, arg);
    } else {
        emitBytes(OP_GET_GLOBAL, arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
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
  [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
  [TOKEN_STRING]        = {string, NULL, PREC_NONE},
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

// starts at the current token and parses any expression at the given precedence level or higher
static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expected expression11");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target");
    }
}

// takes the given token and adds its lexeme to the chunk’s constant table as a string and then returns the index of that constant in the constant table
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// initializes next available local in the compiler's array of variables
// stores the var name and depth of the scope that owns the variable
static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = current->scopeDepth;
}

static void declareVariable() {
    if (current->scopeDepth == 0) return; // only declare locals if we're not in the top level global scope

    Token* name = &parser.previous;

    // prevent declaring variables twice (e.g. int a=1; int a=2;)
    for (int i = current->localCount -1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope");
        }
    }

    addLocal(*name);
}

// parses a variable
static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0; // exit if we're in a local scope

    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

// returns rule from parse function table
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// parses from the lowest to highest precedence level
static void expression() {
    parsePrecedence(PREC_ASSIGNMENT); 
}

// handler for statements that consist of a single expression followed by a semicolon
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after value.");
    emitByte(OP_POP); // we added into the hashmap so we can clear from the stack 
}

// keep parsing declarations and statements until it hits the closing brace
static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after block");
}

static void varDeclaration() {
    uint8_t global = parseVariable("Expected variable name");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration");

    defineVariable(global);
}

static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after value.");
    emitByte(OP_PRINT);
}

// recover from this panicMode and continue parsing at a logical point in the source code, rather than halting entirely or producing a cascade of errors
// we skip tokens indiscriminately until statement boundary
// aka error synchronization
static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default: 
                // do nothing
                ;

        }

        advance();
    }
}

static void declaration() {
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

// dispatcher that determines what kind of statement it's looking at and calls the appropriate handler
static void statement() {
    if(match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_LEFT_BRACE)) { // parse intitial curly brace
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler);
    compilingChunk = chunk;

    // reset from previous compile
    parser.hadError = false;
    parser.panicMode = false;

    advance(); // prime the pump

    // We keep compiling declarations until we hit the end of the source file
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    // expression(); // parse single expression
    // consume(TOKEN_EOF, "Expected end of expression"); // check for EOF token
    endCompiler();
    return !parser.hadError;
}