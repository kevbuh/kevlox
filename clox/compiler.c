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
    int depth;  // depth of the block where the local variable was declared
} Local;

typedef struct {
    uint8_t index; // stores which local slot the upvalue is capturing 
    bool isLocal;  // controls whether the closure captures a local variable or an upvalue from the surrounding function.
} Upvalue;

// lets the compiler tell when it’s compiling top-level code versus the body of a function
typedef enum FunctionType {
    TYPE_FUNCTION,
    TYPE_SCRIPT
} FunctionType;

// simple flat array of all locals that are in scope during each point in the compilation process
typedef struct Compiler {
    struct Compiler* enclosing;    // compiler for the enclosing scope (e.g., outer function)
    ObjFunction* function;         // the function being compiled
    FunctionType type;             // type of the function (e.g., top-level, method, lambda)

    Local locals[UINT8_COUNT];     // local variables in the current scope
    int localCount;                // number of locals in use
    Upvalue upvalues[UINT8_COUNT]; // upvalues captured by the function (for closures)
    int scopeDepth;                // current nesting level of scopes
} Compiler;

Parser parser;
Compiler* current = NULL; // global compiler object

static Chunk* currentChunk() {
    return &current->function->chunk;
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

// =============== emit ===============

// writes the given byte to the chunk, which may be an opcode or an operand to an instruction
// also sends in the previous token’s line information so that runtime errors are associated with that line
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

// writes loop to byte chunk
static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff); // top byte
    emitByte(offset & 0xff); // bottom byte
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction); // writes the opcode for the jump instruction to the current chunk of bytecode
    emitByte(0xff); // placeholder
    emitByte(0xff); // placeholder
    return currentChunk()->count - 2; // returns the index of the first placeholder byte
}

static void emitReturn() {
    emitByte(OP_NIL);
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

// backpatching: replaces the placeholder with the correct offset once it’s known
static void backpatchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Jump limit UINT16_MAX exceeded");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff; // Extracts the high byte of the 16-bit jump offset
    currentChunk()->code[offset + 1] = jump & 0xff; // Extracts the low byte of the 16-bit jump offset
}

// =============== compiler helpers ===============

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;

    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();

    current = compiler;

    // set function name, we've already parsed it
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    // initialize the first local variable slot
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
}

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;

    #ifdef DEBUG_PRINT_CODE
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
    #endif

    current = current->enclosing;
    return function;
}

// =============== scope ===============

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

// =============== forward declarations ===============

static void expression();
static void statement();
static void declaration();
static uint8_t identifierConstant(Token* name);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static int resolveLocal(Compiler* compiler, Token* name);
static void and_(bool canAssign);
static void varDeclaration();
static uint8_t argumentList();
static int resolveUpvalue(Compiler* compiler, Token* name);

// =============== ops ===============

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

static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
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

static void or_(bool canAssign) {
  // Jump to parse the second operand if the first is falsy
  int elseJump = emitJump(OP_JUMP_IF_FALSE);

  // Skip parsing the second operand if the first is truthy
  int endJump = emitJump(OP_JUMP);

  backpatchJump(elseJump);      // Patch to continue parsing the second operand
  emitByte(OP_POP);         // Pop the first operand

  parsePrecedence(PREC_OR); // Parse the second operand
  backpatchJump(endJump);       // Patch to skip the second operand if already evaluated
}


static void string(bool canAssign) {
    // + trim lead  quotation mark
    // - trim trailing quotation mark
    // create a string object, wrap it ina value, stuffs it into the constant table
    emitConstant(OBJ_VAL(copyString(parser.previous.start+1, parser.previous.length-2)));
}

static void namedVariable(Token name, bool canAssign) {
    // uint8_t arg = identifierConstant(&name);
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) { // look for outer scopes (for closures)
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
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

// =============== parse table ===============

// {prefix fn, infix fn, infix precedence}
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_CALL},
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
  [TOKEN_AND]           = {NULL, and_, PREC_AND},
  [TOKEN_CLASS]         = {NULL, NULL, PREC_NONE},
  [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
  [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
  [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
  [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
  [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
  [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
  [TOKEN_OR]            = {NULL, or_, PREC_OR},
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

// walk the list of locals that are currently in scope
static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer");
            }
            return i;
        }
    }

    // variable wasn’t found in local scope, should be assumed to be a global variable instead.
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) { // upvalue already in Upvalue array
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

// looks for a local variable declared in any of the surrounding functions
static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
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
    local->depth = -1;
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

static void markInitialized() {
    if (current->scopeDepth == 0) return; // function bound to a global variable
    current->locals[current->localCount-1].depth = current->scopeDepth;
}

// define global variable and writes op code to chunk
static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

// returns the number of arguments it compiled
static uint8_t argumentList() {
    uint8_t argCount = 0;

    // parse arguments
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
    return argCount;
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    backpatchJump(endJump);
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

static void forStatement() {
  // new scope for the for loop
  beginScope();

  // consume the opening parenthesis after 'for'
  consume(TOKEN_LEFT_PAREN, "Expected '(' after 'for'.");

  // Handle the initializer part of the for loop
  if (match(TOKEN_SEMICOLON)) {
    // No initializer present
  } else if (match(TOKEN_VAR)) {
    // variable declaration as the initializer
    varDeclaration();
  } else {
    // expression as the initializer
    expressionStatement();
  }

  // start of the loop body
  int loopStart = currentChunk()->count;

  // Handle exit condition of the for loop
  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    // Parse the exit condition expression
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' in for loop condition.");

    // Emit a jump instruction to exit the loop if the condition is false
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop the condition value from the stack
  }

  // Handle increment part of the for loop
  if (!match(TOKEN_RIGHT_PAREN)) {
    // Jump to loop body
    int bodyJump = emitJump(OP_JUMP);

    // Mark start of the increment expression
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP); // Pop the increment value from the stack
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'for' clause.");

    // Emit a loop instruction to jump back to the start of the loop
    emitLoop(loopStart);
    loopStart = incrementStart;
    backpatchJump(bodyJump);
  }

  // Parse the loop body statement
  statement();

  // Emit a loop instruction to jump back to the start of the loop
  emitLoop(loopStart);

  // Patch the exit jump if an exit condition was present
  if (exitJump != -1) {
    backpatchJump(exitJump);
    emitByte(OP_POP); // Pop the condition value from the stack
  }

  // End the scope of the for loop
  endScope();
}

static void ifStatement() {
    // compile the condition expression bracketed by parentheses
    // at runtime the condition value will be at the top of the stack, which we can use to execute the then branch or skip it
    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if' statement");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after 'if' statement");

    // how much to offset the instruction pointer in bytes of code to skip if false
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    // else
    int elseJump = emitJump(OP_JUMP);
    backpatchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    backpatchJump(elseJump); // ensures the jump from the then block skips over the else block
}

// keep parsing declarations and statements until it hits the closing brace
static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after block");
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope(); 

    // (...,...,...
    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
        current->function->arity++;
        if (current->function->arity > 255) {
            errorAtCurrent("Can't have more than 255 parameters.");
        }
        uint8_t constant = parseVariable("Expected parameter name.");
        defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    // ...)
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");
    // { ...
    consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    // emitBytes(OP_CONSTANT, makeConstant(OBJ_VAL(function)));
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void funDeclaration() {
    uint8_t global = parseVariable("Expected function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
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

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expected ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void whileStatement() {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    backpatchJump(exitJump);
    emitByte(OP_POP);
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
    if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
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
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) { // parse intitial curly brace
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    // reset from previous compile
    parser.hadError = false;
    parser.panicMode = false;

    advance(); // prime the pump

    // We keep compiling declarations until we hit the end of the source file
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}