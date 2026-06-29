#include "ring_buffer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static int g_count, g_fail;
#define CHECK(cond) do { g_count++; if(!(cond)){ g_fail++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

static bool collect_cb(const uint8_t *buf, size_t len, int64_t ts, void *ctx) {
    int64_t *arr = (int64_t*)ctx;      // arr[0]=n, then ts values
    arr[1 + arr[0]] = ts; arr[0]++;
    (void)buf; (void)len;
    return true;
}

static void test_push_and_stats(void) {
    ring_buffer_t *rb = rb_create(1024, 60000);
    uint8_t a[10] = {0};
    CHECK(rb_push(rb, a, 10, 1000));
    CHECK(rb_push(rb, a, 20, 2000));
    size_t n, b; int64_t span;
    rb_stats(rb, &n, &b, &span);
    CHECK(n == 2); CHECK(b == 30); CHECK(span == 1000);
    rb_destroy(rb);
}

static void test_reject_bad_input(void) {
    ring_buffer_t *rb = rb_create(100, 60000);
    uint8_t a[10] = {0};
    CHECK(!rb_push(rb, NULL, 10, 1));   // null
    CHECK(!rb_push(rb, a, 0, 1));        // zero len
    CHECK(!rb_push(rb, a, 101, 1));      // larger than budget
    size_t n, b; int64_t span; rb_stats(rb, &n, &b, &span);
    CHECK(n == 0);
    rb_destroy(rb);
}

static void test_byte_eviction_drops_oldest(void) {
    ring_buffer_t *rb = rb_create(100, 600000);
    uint8_t a[60] = {0};
    CHECK(rb_push(rb, a, 60, 1000));   // [60]
    CHECK(rb_push(rb, a, 60, 2000));   // 120>100 -> evict 1000 -> [60]
    size_t n, b; int64_t span; rb_stats(rb, &n, &b, &span);
    CHECK(n == 1); CHECK(b == 60);
    int64_t out[8] = {0};
    rb_foreach(rb, collect_cb, out);
    CHECK(out[0] == 1);
    CHECK(out[1] == 2000);             // oldest survivor is the 2000 frame
    rb_destroy(rb);
}

static void test_time_window_eviction(void) {
    ring_buffer_t *rb = rb_create(100000, 60000);
    uint8_t a[10] = {0};
    rb_push(rb, a, 10, 1000);
    rb_push(rb, a, 10, 5000);
    rb_push(rb, a, 10, 70000);        // 70000-1000=69000>60000 -> evict 1000;
                                       // 70000-5000=65000>60000 -> evict 5000
    size_t n, b; int64_t span; rb_stats(rb, &n, &b, &span);
    CHECK(n == 1); CHECK(span == 0);
    int64_t out[8] = {0}; rb_foreach(rb, collect_cb, out);
    CHECK(out[1] == 70000);
    rb_destroy(rb);
}

static void test_order_preserved(void) {
    ring_buffer_t *rb = rb_create(100000, 600000);
    uint8_t a[10] = {0};
    for (int i = 1; i <= 5; i++) rb_push(rb, a, 10, i * 1000);
    int64_t out[16] = {0}; rb_foreach(rb, collect_cb, out);
    CHECK(out[0] == 5);
    for (int i = 0; i < 5; i++) CHECK(out[1 + i] == (i + 1) * 1000);
    rb_destroy(rb);
}

static void test_newest_ts(void) {
    ring_buffer_t *rb = rb_create(100000, 600000);
    CHECK(rb_newest_ts(rb, -1) == -1);          // empty -> fallback
    uint8_t a[10] = {0};
    rb_push(rb, a, 10, 1000);
    rb_push(rb, a, 10, 4000);
    CHECK(rb_newest_ts(rb, -1) == 4000);        // tail timestamp
    rb_destroy(rb);
}

static void test_copy_next_streams_in_order(void) {
    ring_buffer_t *rb = rb_create(100000, 600000);
    uint8_t a[3] = {0xAA, 0xBB, 0xCC};
    uint8_t b[2] = {0x11, 0x22};
    rb_push(rb, a, 3, 1000);
    rb_push(rb, b, 2, 2000);
    int64_t end = rb_newest_ts(rb, 0);          // 2000
    uint8_t out[8]; size_t olen = 0; int64_t ots = 0;
    int64_t after = INT64_MIN;
    CHECK(rb_copy_next(rb, after, end, out, sizeof(out), &olen, &ots));
    CHECK(olen == 3); CHECK(ots == 1000);
    CHECK(out[0] == 0xAA && out[1] == 0xBB && out[2] == 0xCC);
    after = ots;
    CHECK(rb_copy_next(rb, after, end, out, sizeof(out), &olen, &ots));
    CHECK(olen == 2); CHECK(ots == 2000);
    CHECK(out[0] == 0x11 && out[1] == 0x22);
    after = ots;
    CHECK(!rb_copy_next(rb, after, end, out, sizeof(out), &olen, &ots));  // drained
    rb_destroy(rb);
}

static void test_copy_next_respects_upto_and_capacity(void) {
    ring_buffer_t *rb = rb_create(100000, 600000);
    uint8_t a[5] = {0};
    rb_push(rb, a, 5, 1000);
    rb_push(rb, a, 5, 2000);
    rb_push(rb, a, 5, 3000);
    uint8_t out[8]; size_t olen = 0; int64_t ots = 0;
    int64_t after = INT64_MIN;
    CHECK(rb_copy_next(rb, after, 2000, out, sizeof(out), &olen, &ots)); after = ots;  // 1000
    CHECK(rb_copy_next(rb, after, 2000, out, sizeof(out), &olen, &ots)); after = ots;  // 2000
    CHECK(ots == 2000);
    CHECK(!rb_copy_next(rb, after, 2000, out, sizeof(out), &olen, &ots));  // 3000 > upto
    // capacity too small: every frame (len 5) > cap 2 -> all skipped, none returned
    uint8_t small[2];
    CHECK(!rb_copy_next(rb, INT64_MIN, 3000, small, sizeof(small), &olen, &ots));
    rb_destroy(rb);
}

int main(void) {
    test_push_and_stats();
    test_reject_bad_input();
    test_byte_eviction_drops_oldest();
    test_time_window_eviction();
    test_order_preserved();
    test_newest_ts();
    test_copy_next_streams_in_order();
    test_copy_next_respects_upto_and_capacity();
    printf("%d checks, %d failures\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
