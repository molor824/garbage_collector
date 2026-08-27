#pragma once

#include <stddef.h>

typedef struct {
    size_t gen_count;
    size_t *gen_buf_sizes;
} GcInitInfo;

struct GcObj;

typedef void (*GcTraverser)(struct GcObj, void (*)(struct GcObj));

typedef struct GcObj {
    void *data;
    GcTraverser traverser;
} GcObj;

struct GcStack;
typedef struct GcStack {
    struct GcStack *prev;
    size_t count;
    GcObj *objs;
} GcStack;

// Buffer allocates like a stack, from top to bottom.
// This ensures the linked list points from the most recent to most last, allowing fast allocation and iteration for collection
typedef struct {
    void *start;
    void *end;
    void *base;
} GenBuffer;

extern const GcInitInfo GC_DEFAULT_INIT_INFO;
extern size_t *_gc_gen_collect_counts;
extern GenBuffer *_gc_gens;
extern size_t _gc_gen_count;

void gc_collect(size_t gen);
void gc_init(GcInitInfo info);
GcObj gc_alloc(size_t size, GcTraverser traversal);
void gc_push_stack(GcStack *current);
void gc_pop_stack(GcStack *current);
