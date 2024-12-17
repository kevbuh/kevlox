all: build run

build:
	gcc main.c chunk.c memory.c debug.c value.c vm.c compiler.c scanner.c -o clox

run:
	./clox
