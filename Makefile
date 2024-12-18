all: build run

build:
	gcc clox/main.c clox/chunk.c clox/memory.c clox/debug.c clox/value.c clox/vm.c clox/compiler.c clox/scanner.c

run:
	./a.out code.txt