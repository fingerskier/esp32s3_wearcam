#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>

#ifdef RB_USE_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define RB_LOCK(rb)   xSemaphoreTakeRecursive((rb)->mtx, portMAX_DELAY)
#define RB_UNLOCK(rb) xSemaphoreGiveRecursive((rb)->mtx)
#else
#define RB_LOCK(rb)   ((void)0)
#define RB_UNLOCK(rb) ((void)0)
#endif

typedef struct node {
    uint8_t *buf;
    size_t len;
    int64_t ts_ms;
    struct node *next;
} node_t;

struct ring_buffer {
    node_t *head;   // oldest
    node_t *tail;   // newest
    size_t count;
    size_t bytes;
    size_t max_bytes;
    int64_t window_ms;
#ifdef RB_USE_FREERTOS
    SemaphoreHandle_t mtx;
#endif
};

ring_buffer_t *rb_create(size_t max_bytes, int64_t window_ms)
{
    ring_buffer_t *rb = calloc(1, sizeof(*rb));
    if (!rb) return NULL;
    rb->max_bytes = max_bytes;
    rb->window_ms = window_ms;
#ifdef RB_USE_FREERTOS
    rb->mtx = xSemaphoreCreateRecursiveMutex();
#endif
    return rb;
}

static void evict_head(ring_buffer_t *rb)
{
    node_t *n = rb->head;
    if (!n) return;
    rb->head = n->next;
    if (!rb->head) rb->tail = NULL;
    rb->count--;
    rb->bytes -= n->len;
    free(n->buf);
    free(n);
}

void rb_destroy(ring_buffer_t *rb)
{
    if (!rb) return;
    while (rb->head) evict_head(rb);
#ifdef RB_USE_FREERTOS
    if (rb->mtx) vSemaphoreDelete(rb->mtx);
#endif
    free(rb);
}

bool rb_push(ring_buffer_t *rb, const uint8_t *jpeg, size_t len, int64_t ts_ms)
{
    if (!rb || !jpeg || len == 0 || len > rb->max_bytes) return false;
    node_t *n = malloc(sizeof(*n));
    if (!n) return false;
    n->buf = malloc(len);
    if (!n->buf) { free(n); return false; }
    memcpy(n->buf, jpeg, len);
    n->len = len;
    n->ts_ms = ts_ms;
    n->next = NULL;

    RB_LOCK(rb);
    if (rb->tail) rb->tail->next = n; else rb->head = n;
    rb->tail = n;
    rb->count++;
    rb->bytes += len;
    // byte-budget eviction
    while (rb->bytes > rb->max_bytes && rb->head != rb->tail) evict_head(rb);
    // time-window eviction (relative to newest)
    while (rb->head && rb->head != rb->tail &&
           (ts_ms - rb->head->ts_ms) > rb->window_ms) evict_head(rb);
    RB_UNLOCK(rb);
    return true;
}

void rb_stats(ring_buffer_t *rb, size_t *count, size_t *bytes, int64_t *span_ms)
{
    if (!rb) return;
    RB_LOCK(rb);
    if (count) *count = rb->count;
    if (bytes) *bytes = rb->bytes;
    if (span_ms) *span_ms = (rb->head && rb->tail) ? (rb->tail->ts_ms - rb->head->ts_ms) : 0;
    RB_UNLOCK(rb);
}

void rb_foreach(ring_buffer_t *rb, rb_iter_cb cb, void *ctx)
{
    if (!rb || !cb) return;
    RB_LOCK(rb);
    for (node_t *n = rb->head; n; n = n->next) {
        if (!cb(n->buf, n->len, n->ts_ms, ctx)) break;
    }
    RB_UNLOCK(rb);
}
