#include "tracing.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

struct AllocHeader;
typedef struct AllocHeader {
    size_t size; // Total size, including sizeof(AllocHeader), aligned to max_align_t
    union { // During tracing stage, it will be represented as next_data
        int reachable;
        void *next_data;
    };
} AllocHeader;

size_t *_gc_gen_collect_counts;
GenBuffer *_gc_gens;
size_t _gc_gen_count;
static GcStack *_current_stack = NULL;

static size_t _default_buf_sizes[] = {0x100000, 0x800000, 0x1000000};
const GcInitInfo GC_DEFAULT_INIT_INFO = {
    .gen_count = sizeof(_default_buf_sizes) / sizeof(_default_buf_sizes[0]),
    .gen_buf_sizes = _default_buf_sizes,
};

static inline size_t align_up(size_t size, size_t align) {
    return (size + align - 1) / align * align;
}

void gc_init(GcInitInfo info) {
    _gc_gens = malloc(sizeof(GenBuffer) * info.gen_count);
    _gc_gen_collect_counts = calloc(info.gen_count, sizeof(size_t));
    _gc_gen_count = info.gen_count;
    for (size_t i = 0; i < _gc_gen_count; i++) {
        GenBuffer *buffer = _gc_gens + i;
        size_t buf_cap = align_up(info.gen_buf_sizes[i], alignof(max_align_t));
        buffer->start = calloc(buf_cap, 1);
        buffer->base = buffer->end = buffer->start + buf_cap;
    }
}

static void *_gc_filter_start, *_gc_filter_end;
static void _gc_traverse(GcObj root) {
    if (!root.data) return; // Null object

    AllocHeader *header = root.data - sizeof(AllocHeader);
    // If header is outside of filter range, then simply return because not worth marking objects outside of this generation
    if ((void*)header < _gc_filter_start || (void*)header >= _gc_filter_end) return;

    header->reachable = 1; // mark as reachable
    if (root.traverser) root.traverser(root, _gc_traverse);
}

static AllocHeader *_gc_try_alloc(size_t gen, size_t size);
void gc_collect(size_t gen) {
    // Collect current generation, and move everything to the next one (or leak it)
    // If the current gen is more than _gen_count, this function does nothing
    if (gen >= _gc_gen_count) return;

    GenBuffer *buf = _gc_gens + gen;

    // Setup for traversing
    _gc_filter_start = buf->base;
    _gc_filter_end = buf->end;

    // Traverse stack tree
    for (GcStack *stack = _current_stack; stack; stack = stack->prev) {
        // for each object, check if it's in the current gen
        for (size_t i = 0; i < stack->count; i++) {
            _gc_traverse(stack->objs[i]);
        }
    }

    // Tracing stage
    // If it's marked, allocate same memory to the next buffer, which probably will trigger another collection if that one is full
    // In order to prevent new pointers from being collected during the conversion of buffer, create a temporary stack
    size_t count = 0;
    for (AllocHeader *header = buf->base; (void*)header < buf->end; header = (void*)header + header->size) {
        if (header->reachable) count++;
    }
    GcObj objs[count] = {}; // Default initialization to null
    GcStack stack = {
        .count = count,
        .objs = objs,
    };
    gc_push_stack(&stack); // Make sure the allocation wont collect it
    count = 0;
    for (AllocHeader *header = buf->base; (void*)header < buf->end; header = (void*)header + header->size) {
        assert(header->size);
        if (!header->reachable) continue;

        objs[count].data = (void*)_gc_try_alloc(gen + 1, header->size - sizeof(AllocHeader)) + sizeof(AllocHeader);
        header->next_data = objs[count].data;
        count++;
    }
    gc_pop_stack(&stack); // We can safely pop because no allocation is gonna be imminent enough to cause relocation during the trace stage

    // NOTE: FROM THIS POINT ON reachable IS INVALID FOR THIS GENERATION
    // Start traversing the stack, and update the moved pointers
    for (GcStack *stack = _current_stack; stack; stack = stack->prev) {
        for (size_t i = 0; i < stack->count; i++) {
            GcObj *obj = &stack->objs[i];
            AllocHeader *header = (AllocHeader*)(obj->data - sizeof(AllocHeader));
            if ((void*)header < buf->base || (void*)header >= buf->end) continue; // If outside of this generation range, then skip
            obj->data = header->next_data; // Update object pointer if the object is part of this generation (which is being collected)
            header->reachable = 0; // Reset reachable
        }
    }
    // Reset the base header to end
    buf->base = buf->end;

    // printf("collected gen %lu with %lu amount of objects moved to next gen\n", gen, count);
    _gc_gen_collect_counts[gen]++;
}
static AllocHeader *_gc_try_alloc(size_t gen, size_t size) {
    // Size that rounds up to align of allocheader
    size_t total_size = align_up(sizeof(AllocHeader) + size, alignof(max_align_t));

    if (gen >= _gc_gen_count) {
        // Regular allocation with memory leaking
        AllocHeader *header = malloc(total_size);
        header->size = total_size;
        header->reachable = 0;
        return header;
    }

    GenBuffer *buf = _gc_gens + gen;

    if (buf->start + total_size > buf->end) {
        // If the total size is so large that it can't even fit the buffer
        // Try to allocate on the next buffer
        return _gc_try_alloc(gen + 1, size);
    }
    if (buf->start + total_size > buf->base) {
        // Memory overflow, collect in this generation
        gc_collect(gen);
        // Make sure the base points at the end of the buffer
        assert(buf->base == buf->end);
    }

    AllocHeader *base = (AllocHeader*)(buf->base -= total_size);
    base->size = total_size;
    base->reachable = 0;

    return base;
}
GcObj gc_alloc(size_t size, GcTraverser traverser) {
    void *header = _gc_try_alloc(0, size);
    return (GcObj){
        .data = header + sizeof(AllocHeader),
        .traverser = traverser,
    };
}
void gc_push_stack(GcStack *current) {
    current->prev = _current_stack;
    _current_stack = current;
}
void gc_pop_stack(GcStack *current) {
    _current_stack = current->prev;
}
