#include "gc/tracing.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    size_t size;
    GcObj objs[];
} Array;

void array_traverser(GcObj this, void (*traverse)(GcObj)) {
    Array *arr = this.data;
    for (size_t i = 0; i < arr->size; i++) {
        traverse(arr->objs[i]);
    }
}

GcObj init_array(size_t size) {
    GcObj obj = gc_alloc(sizeof(Array) + size * sizeof(GcObj), array_traverser);
    Array *arr = obj.data;
    arr->size = size;
    memset(arr->objs, 0, size * sizeof(GcObj));
    return obj;
}

int main() {
    size_t gen_sizes[] = {0x10000, 0x100000};
    gc_init((GcInitInfo){
        .gen_buf_sizes = gen_sizes,
        .gen_count = 2,
    });

    srand(42);

    GcObj objs[1] = {};
    GcStack stack = {
        .count = sizeof(objs) / sizeof(GcObj),
        .objs = objs,
    };

    gc_push_stack(&stack);

    GcObj * const arr = objs;

    const size_t slot = 10000;
    const size_t n = 10000000;
    *arr = init_array(slot);

    for (size_t i = 0; i < n; i++) {
        size_t idx = rand() % slot;
        size_t size = 8 + (rand() % 0x1000);
        GcObj init = gc_alloc(size, NULL);
        memset(init.data, size, size);
        ((Array*)arr->data)->objs[idx] = init;
    }

    gc_pop_stack(&stack);

    for (size_t i = 0; i < 3; i++) {
        printf("gen %lu: %lu\n", i, _gc_gen_collect_counts[i]);
    }
}
