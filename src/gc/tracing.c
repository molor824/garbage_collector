#include "tracing.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

struct AllocHeader;
typedef struct AllocHeader {
    size_t size; // Total size, including sizeof(AllocHeader), aligned to max_align_t
    union { // During tracing stage, it will be represented as next_data
        int reachable;
        void *next_data;
    };
} AllocHeader;

// Buffer allocates like a stack, from top to bottom.
// This ensures the linked list points from the most recent to most last, allowing fast allocation and iteration for collection
typedef struct {
    void *start;
    void *end;
    void *base;
} GenBuffer;

size_t *_gc_gen_collect_counts;
size_t _gc_gen_count;
GcStackObj *gc_stack_root = NULL;
static GenBuffer *_gc_gens;

static inline size_t align_up(size_t size, size_t align) {
    assert(align >= 1);
    return (size + align - 1) / align * align;
}

void gc_init(GcInitInfo info) {
    _gc_gens = malloc(sizeof(GenBuffer) * info.gen_count);
    _gc_gen_collect_counts = calloc(info.gen_count, sizeof(size_t));
    _gc_gen_count = info.gen_count;
    for (size_t i = 0; i < _gc_gen_count; i++) {
        GenBuffer *buffer = _gc_gens + i;
        size_t buf_cap = align_up(info.gen_buf_sizes[i], alignof(max_align_t));
        buffer->start = malloc(buf_cap);
        buffer->base = buffer->end = buffer->start + buf_cap;
    }
}

static GenBuffer _gc_traverse_buf;

static void _gc_traverse_mark(GcObj *root) {
    if (!root || !root->data) return; // Null object

    AllocHeader *header = root->data - sizeof(AllocHeader);
    // If header is outside of filter range, then simply return because not worth marking objects outside of this generation
    // Or if its already marked, then it's most likely that it's a cyclic reference
    if ((void*)header < _gc_traverse_buf.base || (void*)header >= _gc_traverse_buf.end || header->reachable) return;

    header->reachable = 1; // mark as reachable
    if (root->traverser) root->traverser(root, _gc_traverse_mark);
}

static void _gc_traverse_relocate(GcObj *root) {
    if (!root || !root->data) return;

    AllocHeader *header = root->data - sizeof(AllocHeader);
    // If header is outside of buf range, then it's not this generation's object that requires relocation, skip
    if ((void*)header < _gc_traverse_buf.base || (void*)header >= _gc_traverse_buf.end) return;

    root->data = header->next_data;
    if (root->traverser) root->traverser(root, _gc_traverse_relocate);
}

static AllocHeader *_gc_try_alloc(size_t gen, size_t size);
void gc_collect(size_t gen) {
    // Collect current generation, and move everything to the next one (or leak it)
    // If the current gen is more than _gen_count, this function does nothing
    if (gen >= _gc_gen_count) return;

    GenBuffer *buf = _gc_gens + gen;

    _gc_traverse_buf = *buf;

    // Traverse stack tree
    for (GcStackObj *obj = gc_stack_root; obj; obj = obj->prev) {
        _gc_traverse_mark(&obj->obj);
    }

    // relocation stage
    // If it's marked, allocate same memory to the next buffer
    // In order to prevent collection of memory while being updated, we calculate the total size needed first
    size_t relocate_size = 0;
    for (AllocHeader *header = buf->base; (void*)header < buf->end; header = (void*)header + header->size) {
        if (!header->reachable) continue;
        relocate_size += header->size;
    }

    if (relocate_size) {
        // Since sizeof(AllocHeader) is added
        assert(relocate_size > sizeof(AllocHeader));
        AllocHeader *dst = _gc_try_alloc(gen + 1, relocate_size - sizeof(AllocHeader));

        for (AllocHeader *header = buf->base; (void*)header < buf->end; header = (void*)header + header->size) {
            if (!header->reachable) continue;

            memcpy(dst, header, header->size);
            header->next_data = (void*)dst + sizeof(AllocHeader);
            dst->reachable = 0; // Reset reachable at the new loc
            dst = (void*)dst + header->size;
        }

        // NOTE: FROM THIS POINT ON reachable IS INVALID FOR THIS GENERATION
        // Start traversing the stack, and update the moved pointers
        for (GcStackObj *obj = gc_stack_root; obj; obj = obj->prev) {
            _gc_traverse_relocate(&obj->obj);
        }
    }
    // Reset the base header to end
    buf->base = buf->end;

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

    AllocHeader *base = (AllocHeader*)(buf->base - total_size);
    buf->base = base;
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
