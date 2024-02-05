GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: libcoro.c solution.c vector.c
	gcc $(GCC_FLAGS) libcoro.c solution.c vector.c ../utils/heap_help/heap_help.c

clean:
	rm a.out
