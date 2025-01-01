# clox

<div align="center">
  <img src="https://craftinginterpreters.com/image/a-map-of-the-territory/mountain.png" alt="ci mountain" height="400">
  <br/>
  clox: documented Lox compiler from <a href="https://craftinginterpreters.com/" target="_blank">Crafting Interpreters</a>
  <br/>
</div>

-------

### Compile and run: 

Open ```code.txt``` and insert the following code snippet:

```
fun fib(n) {
  if (n < 2) return n;
  return fib(n - 2) + fib(n - 1);
}

var start = clock();
print "Fib35 result:";
print fib(35);
print "Time (s):";
print clock() - start;
```

To compile and run the code, simply execute the following command in your terminal:

```bash
make all
```

This will process the code, calculate the 35th Fibonacci number, and display both the result and the execution time.

### Features
- source code scanner/lexer
- single pass compiler
- stack-based bytecode VM
- debugging disassembler
- functions are first-class citizens

NOTES:
- binary operators are infix
- unary are prefix
- A compiler has roughly two jobs. Many languages split the two roles into two separate passes
  - It parses the user’s source code to understand what it means
  - Then it takes that knowledge and outputs low-level instructions that produce the same semantics
  - A parser produces an AST and then a code generator traverses the AST and outputs target code
- bytecode > syntax trees
- only supports double-precision floating point numbers
- we want to do as much work as possible during compilation to keep execution simple and fast
- preorder : "on the way down"
- postorder : "on the way back up"

TODO:
- lambda as λ
- let instead of var
- call it bub lang...or lang lang?

bug:
- print 0 or 1 returns 0
  - resolve 0 as false and 1 as true