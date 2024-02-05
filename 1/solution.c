#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct my_context {
	char *name;
	/** ADD HERE YOUR OWN MEMBERS, SUCH AS FILE NAME, WORK TIME, ... */
};

static struct my_context *
my_context_new(const char *name)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

/**
 * A function, called from inside of coroutines recursively. Just to demonstrate
 * the example. You can split your code into multiple functions, that usually
 * helps to keep the individual code blocks simple.
 */
static void
other_function(const char *name, int depth)
{
	printf("%s: entered function, depth = %d\n", name, depth);
	coro_yield();
	if (depth < 3)
		other_function(name, depth + 1);
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
	/* IMPLEMENT SORTING OF INDIVIDUAL FILES HERE. */

	struct coro *this = coro_this();
	struct my_context *ctx = context;
	char *name = ctx->name;
	printf("Started coroutine %s\n", name);
	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	printf("%s: yield\n", name);
	coro_yield();

	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	printf("%s: yield\n", name);
	coro_yield();

	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	other_function(name, 1);
	printf("%s: switch count after other function %lld\n", name,
	       coro_switch_count(this));

	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}

struct vector {
    int size;
    int capacity;
    int *data;
};

void vector_init(struct vector *v) {
    v->size = 0;
    v->capacity = 2;
    v->data = malloc(v->capacity * sizeof(int));
}

void vector_push_back(struct vector *v, int number) {
    if (v->size >= v->capacity) {
        v->capacity *= 1.5;
        v->data = realloc(v->data, v->capacity * sizeof(int));
    }
    v->data[v->size++] = number;
}

void vector_shrink_to_fit(struct vector *v) {
    v->capacity = v->size;
    v->data = realloc(v->data, v->capacity * sizeof(int));
}

void vector_destroy(struct vector *v) {
    free(v->data);
}

void swap(int *lhs, int *rhs) {
    int tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

int partition(struct vector *v, int l, int r) {
    unsigned int pivotIndex = l;
    int pivot = v->data[pivotIndex];
    swap(&v->data[pivotIndex], &v->data[r]);

    int i = l;

    for (int j = l; j < r; ++j) {
        if (v->data[j] <= pivot) {
            swap(&v->data[i], &v->data[j]);
            i += 1;
        }
    }

    swap(&v->data[i], &v->data[r]);

    return i;
}

void quickSort(struct vector *v, int l, int r) {
    if (l >= r)
        return;

    int p = partition(v, l, r);

    quickSort(v, l, p - 1);
    quickSort(v, p + 1, r);
}



int
main(int argc, char **argv)
{
	/* Delete these suppressions when start using the args. */
	(void)argc;
	(void)argv;

    ///////////////////////////////////////////////////////////////////
    // Read
    int num_files = argc - 1;
    struct vector *vectors = malloc(num_files * sizeof(struct vector));
    for (int i = 0; i < num_files; ++i) {
        FILE *fin = fopen(argv[i + 1], "r");
        if (!fin) {
            return -1;
        }

        vector_init(&vectors[i]);
        while (!feof(fin)) {
            int num;
            fscanf(fin, "%d", &num);
            vector_push_back(&vectors[i], num);
        }
        fclose(fin);
        vector_shrink_to_fit(&vectors[i]);
    }

    // Sort
    for (int i = 0; i < num_files; ++i) {
        quickSort(&vectors[i], 0, vectors[i].size - 1);
    }

    // Merge
    /*int min = vectors[0].data[0];*/
    /*int *pos = malloc(num_files * sizeof(int));*/
    /*memset(pos, 0, num_files * sizeof(int));*/



    // Print
    for (int i = 0; i < num_files; ++i) {
        for (int j = 0; j < vectors[i].size; ++j) {
            printf("%d ", vectors[i].data[j]);
        }
        printf("\nEND\n");
    }
    printf("\n");

    // Destroy
    for (int i = 0; i < num_files; ++i) {
        vector_destroy(&vectors[i]);
    }
    free(vectors);
    ///////////////////////////////////////////////////////////////////

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();
	/* Start several coroutines. */
	for (int i = 0; i < 3; ++i) {
		/*
		 * The coroutines can take any 'void *' interpretation of which
		 * depends on what you want. Here as an example I give them
		 * some names.
		 */
		char name[16];
		sprintf(name, "coro_%d", i);
		/*
		 * I have to copy the name. Otherwise all the coroutines would
		 * have the same name when they finally start.
		 */
		coro_new(coroutine_func_f, my_context_new(name));
	}
	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */

	/* IMPLEMENT MERGING OF THE SORTED ARRAYS HERE. */

	return 0;
}
