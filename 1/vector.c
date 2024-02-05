#include "vector.h"

#include <stdlib.h>

void
vector_init(struct vector *v)
{
    v->size = 0;
    v->capacity = 2;
    v->data = malloc(v->capacity * sizeof(int));
}

void
vector_push_back(struct vector *v, int number)
{
    if (v->size >= v->capacity) {
        v->capacity *= 1.5;
        v->data = realloc(v->data, v->capacity * sizeof(int));
    }
    v->data[v->size++] = number;
}

void
vector_shrink_to_fit(struct vector *v)
{
    v->capacity = v->size;
    v->data = realloc(v->data, v->capacity * sizeof(int));
}

void
vector_destroy(struct vector *v)
{
    free(v->data);
}
