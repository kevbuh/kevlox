# clox

<div align="center">
  <img src="https://craftinginterpreters.com/image/a-map-of-the-territory/mountain.png" alt="ci mountain" height="400">
  <br/>
  clox: documented Lox compiler from <a href="https://craftinginterpreters.com/" target="_blank">Crafting Interpreters</a>
  <br/>
</div>

-------

### How to run: 

Go into code.txt and type in the code you want to run. Then run:

```bash
make all
```

### Features
- source code scanner/lexer
- single pass compiler
- stack-based bytecode VM
- debugging disassembler

NOTES:
- binary operators are infix
- unary are prefix
- A compiler has roughly two jobs. Many languages split the two roles into two separate passes
  - It parses the user’s source code to understand what it means
  - Then it takes that knowledge and outputs low-level instructions that produce the same semantics
  - A parser produces an AST and then a code generator traverses the AST and outputs target code
- bytecode > syntax trees
- only supports double-precision floating point numbers
- λ

bug:
- print 0 or 1 returns 0