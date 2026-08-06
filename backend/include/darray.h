#ifndef CO1_DARRAY_H
#define CO1_DARRAY_H

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Minimal type-generic dynamic array for POD structs.
   Usage:
       typedef struct { ... } Foo;
       DARRAY_DECLARE(Foo, FooArray);
       // in one .c file:
       DARRAY_DEFINE(Foo, FooArray);
       FooArray a = {0};
       FooArray_push(&a, foo);
*/
#define DARRAY_DECLARE(type, name)                                     \
    typedef struct name {                                              \
        type *items;                                                   \
        size_t len;                                                    \
        size_t cap;                                                    \
    } name;                                                            \
    void name##_push(name *a, type v);                                 \
    void name##_free(name *a);

#define DARRAY_DEFINE(type, name)                                      \
    void name##_push(name *a, type v) {                                \
        if (a->len == a->cap) {                                        \
            size_t new_cap = a->cap ? a->cap * 2 : 8;                  \
            a->items = (type *)realloc(a->items, new_cap * sizeof(type)); \
            if (!a->items) {                                           \
                fprintf(stderr, "darray: out of memory\n");            \
                exit(1);                                               \
            }                                                          \
            a->cap = new_cap;                                          \
        }                                                              \
        a->items[a->len++] = v;                                        \
    }                                                                  \
    void name##_free(name *a) {                                        \
        free(a->items);                                                \
        a->items = NULL;                                               \
        a->len = 0;                                                    \
        a->cap = 0;                                                    \
    }

#endif /* CO1_DARRAY_H */
