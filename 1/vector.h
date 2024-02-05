#ifndef VECTOR_H
#define VECTOR_H

struct vector {
    int size;
    int capacity;
    int *data;
};

void
vector_init(struct vector *v);

void
vector_push_back(struct vector *v, int number);

void
vector_shrink_to_fit(struct vector *v);

void
vector_destroy(struct vector *v);

#endif /* VECTOR_H */
