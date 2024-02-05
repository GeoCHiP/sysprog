#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libcoro.h"

#include "vector.h"

static void
swap(int *lhs, int *rhs)
{
    int tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

static int
partition(struct vector *v, int l, int r)
{
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

static void
quicksort(struct vector *v, int l, int r)
{
    if (l >= r)
        return;

    int p = partition(v, l, r);

    quicksort(v, l, p - 1);
    quicksort(v, p + 1, r);

    coro_yield();
}

struct my_context {
	char *name;
	const char *input_file_path;
    struct vector *vector;
};

static struct my_context *
my_context_new(const char *name, const char *input_file_path, struct vector *vector)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->input_file_path = input_file_path;
	ctx->vector = vector;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

static int
coroutine_func_f(void *context)
{
	struct my_context *ctx = context;
	char *name = ctx->name;
	const char *input_file_path = ctx->input_file_path;
	struct vector *vector = ctx->vector;

	printf("Started coroutine %s\n", name);

    FILE *fin = fopen(input_file_path, "r");
    if (!fin) {
        return -1;
    }

    vector_init(vector);
    while (!feof(fin)) {
        int num;
        fscanf(fin, "%d", &num);
        vector_push_back(vector, num);
    }
    fclose(fin);
    vector_shrink_to_fit(vector);

    quicksort(vector, 0, vector->size - 1);

	my_context_delete(ctx);
	return 0;
}

int
main(int argc, char **argv)
{
    struct timespec ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts1);

	coro_sched_init();

    int num_files = argc - 1;
    struct vector *vectors = malloc(num_files * sizeof(struct vector));
	for (int i = 0; i < num_files; ++i) {
		char name[16];
		sprintf(name, "coro_%d", i);
		coro_new(coroutine_func_f, my_context_new(name, argv[i + 1], &vectors[i]));
	}

	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		printf("Finished %d\n", coro_status(c));
        printf("Switch count %lld\n", coro_switch_count(c));
		coro_delete(c);
	}

    // Merge
    int *pos = malloc(num_files * sizeof(int));
    memset(pos, 0, num_files * sizeof(int));

    FILE *fout = fopen("out.txt", "w");
    if (!fout) {
        return -1;
    }

    while (true) {
        int min;
        int *min_pos = NULL;
        for (int i = 0; i < num_files; ++i) {
            if (pos[i] < vectors[i].size) {
                min = vectors[i].data[pos[i]];
                min_pos = &pos[i];
                break;
            }
        }
        if (min_pos == NULL) {
            break;
        }

        for (int i = 0; i < num_files; ++i) {
            if (pos[i] >= vectors[i].size) {
                continue;
            }
            if (vectors[i].data[pos[i]] <= min) {
                min = vectors[i].data[pos[i]];
                min_pos = &pos[i];
            }
        }

        *min_pos += 1;
        fprintf(fout, "%d ", min);
    }
    fprintf(fout, "\n");

    // Destroy
    fclose(fout);
    free(pos);
    for (int i = 0; i < num_files; ++i) {
        vector_destroy(&vectors[i]);
    }
    free(vectors);

    struct timespec ts2;
    clock_gettime(CLOCK_MONOTONIC, &ts2);

    printf("Total work time: %ld ns\n", ts2.tv_nsec - ts1.tv_nsec);

	return 0;
}
