#pragma once

#include <stddef.h>

typedef struct {
    size_t gen_count;
    size_t *gen_buf_sizes;
} GcInitInfo;

struct GcObj;

typedef void (*GcTraverser)(struct GcObj*, void (*)(struct GcObj*));

typedef struct GcObj {
    void *data;
    GcTraverser traverser;
} GcObj;

struct GcStackObj;
typedef struct GcStackObj {
    struct GcStackObj *prev;
    GcObj obj;
} GcStackObj;

extern size_t *_gc_gen_collect_counts;
extern size_t _gc_gen_count;
extern GcStackObj *gc_stack_root;

void gc_collect(size_t gen);
void gc_init(GcInitInfo info);
GcObj gc_alloc(size_t size, GcTraverser traversal);
