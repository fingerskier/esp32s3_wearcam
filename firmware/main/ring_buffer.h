#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ring_buffer ring_buffer_t;

// max_bytes: total stored JPEG bytes budget. window_ms: max age of oldest frame
// relative to newest. Eviction drops oldest until BOTH bounds satisfied.
ring_buffer_t *rb_create(size_t max_bytes, int64_t window_ms);
void rb_destroy(ring_buffer_t *rb);

// Copies `len` bytes. Evicts oldest frames as needed. Returns false (no store)
// if jpeg==NULL, len==0, or len > max_bytes.
bool rb_push(ring_buffer_t *rb, const uint8_t *jpeg, size_t len, int64_t ts_ms);

// count = frames, bytes = total stored bytes, span_ms = newest.ts - oldest.ts.
void rb_stats(ring_buffer_t *rb, size_t *count, size_t *bytes, int64_t *span_ms);

// Iterate oldest -> newest. Stop early if cb returns false.
typedef bool (*rb_iter_cb)(const uint8_t *buf, size_t len, int64_t ts_ms, void *ctx);
void rb_foreach(ring_buffer_t *rb, rb_iter_cb cb, void *ctx);
