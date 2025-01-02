all: build run

build:
	gcc kevlox/main.c kevlox/chunk.c kevlox/memory.c kevlox/debug.c kevlox/value.c kevlox/vm.c kevlox/compiler.c kevlox/scanner.c kevlox/object.c kevlox/table.c

run:
	echo ""
	./a.out code.kev
	rm a.out