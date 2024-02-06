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

struct my_context {
    char *name;
    const char *input_file_path;
    struct vector *vector;
    struct timespec timer;
    struct timespec prev_ts;
    long time_slice;
};

static void
stop_timer(struct my_context *ctx)
{
    struct timespec current_ts;
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
    ctx->timer.tv_sec += current_ts.tv_sec - ctx->prev_ts.tv_sec;
    ctx->timer.tv_nsec += current_ts.tv_nsec - ctx->prev_ts.tv_nsec;
}

static void
start_timer(struct my_context *ctx)
{
    clock_gettime(CLOCK_MONOTONIC, &ctx->prev_ts);
}

static bool
is_time_quantum_over(struct my_context *ctx)
{
    struct timespec current_ts;
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
    long elapsed = (current_ts.tv_sec - ctx->prev_ts.tv_sec) * 1000000000 + (current_ts.tv_nsec - ctx->prev_ts.tv_nsec);
    elapsed /= 1000;
    return elapsed >= ctx->time_slice;
}

static void
quicksort(struct vector *v, int l, int r, struct my_context *ctx)
{
    if (l >= r)
        return;

    int p = partition(v, l, r);

    quicksort(v, l, p - 1, ctx);
    quicksort(v, p + 1, r, ctx);

    if (is_time_quantum_over(ctx)) {
        stop_timer(ctx);
        coro_yield();
        start_timer(ctx);
    }
}

static struct my_context *
my_context_new(const char *name, const char *input_file_path, struct vector *vector, long time_slice)
{
    struct my_context *ctx = malloc(sizeof(*ctx));
    ctx->name = strdup(name);
    ctx->input_file_path = input_file_path;
    ctx->vector = vector;
    memset(&ctx->timer, 0, sizeof(ctx->timer));
    memset(&ctx->prev_ts, 0, sizeof(ctx->prev_ts));
    ctx->time_slice = time_slice;
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

    printf("%s: started\n", name);
    start_timer(ctx);

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

    quicksort(vector, 0, vector->size - 1, ctx);

    stop_timer(ctx);

    printf("%s: work time %ld s %ld ns\n", name, ctx->timer.tv_sec, ctx->timer.tv_nsec);

    my_context_delete(ctx);
    return 0;
}

int
main(int argc, char **argv)
{
    struct timespec ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts1);

    coro_sched_init();

    long target_latency = atol(argv[1]);
    int num_files = argc - 2;
    struct vector *vectors = malloc(num_files * sizeof(struct vector));
    for (int i = 0; i < num_files; ++i) {
        char name[16];
        sprintf(name, "coro_%d", i);
        coro_new(coroutine_func_f, my_context_new(name, argv[i + 2], &vectors[i], target_latency / num_files));
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

    printf("Total work time: %ld s %ld ns\n", ts2.tv_sec - ts1.tv_sec, ts2.tv_nsec - ts1.tv_nsec);

    return 0;
}
