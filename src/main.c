#include "gc/tracing.h"

#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    size_t size;
    GcObj objs[];
} Array;

void array_traverser(GcObj *this, void (*traverse)(GcObj*)) {
    Array *arr = this->data;
    for (size_t i = 0; i < arr->size; i++) {
        traverse(&arr->objs[i]);
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
    size_t gen_sizes[] = {0x10000, 0x10000, 0x100000, 0x1000000};
    gc_init((GcInitInfo){
        .gen_buf_sizes = gen_sizes,
        .gen_count = sizeof(gen_sizes) / sizeof(gen_sizes[0]),
    });

    srand(69);

    const size_t slot = 1000;
    const size_t n = 10000000;

    GcStackObj arr = {
        .obj = init_array(slot),
        .prev = gc_stack_root,
    };
    GcStackObj init = {
        .prev = &arr
    };
    gc_stack_root = &init;

    for (size_t i = 0; i < n; i++) {
        size_t idx = rand() % slot;
        size_t size = 8 + (rand() % 0x1000);
        init.obj = gc_alloc(size, NULL),

        memset(init.obj.data, i, size);
        ((Array*)arr.obj.data)->objs[idx] = init.obj;
    }

    gc_stack_root = arr.prev;

    for (size_t i = 0; i < _gc_gen_count; i++) {
        printf("gen %lu: %lu\n", i, _gc_gen_collect_counts[i]);
    }
}
