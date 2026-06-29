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
// NOTE: the callback runs while the buffer lock is held — keep it short and
// non-blocking. For streaming frames over a (slow) network, use rb_copy_next
// instead so the lock is released between frames.
typedef bool (*rb_iter_cb)(const uint8_t *buf, size_t len, int64_t ts_ms, void *ctx);
void rb_foreach(ring_buffer_t *rb, rb_iter_cb cb, void *ctx);

// Timestamp of the newest frame, or `fallback` if the buffer is empty.
int64_t rb_newest_ts(ring_buffer_t *rb, int64_t fallback);

// Copy the oldest frame whose timestamp satisfies after_ts < ts <= upto_ts
// into `out` (capacity out_cap). On success sets *out_len and *out_ts and
// returns true; frames larger than out_cap are skipped. Returns false when no
// such frame exists. Designed for lock-free streaming: call repeatedly, each
// time passing the previous *out_ts as after_ts and a fixed upto_ts (snapshot
// rb_newest_ts at the start) to bound the run. The lock is held only for the
// per-frame copy, never across the caller's processing.
bool rb_copy_next(ring_buffer_t *rb, int64_t after_ts, int64_t upto_ts,
                  uint8_t *out, size_t out_cap, size_t *out_len, int64_t *out_ts);
