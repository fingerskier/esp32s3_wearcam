#include "ring_buffer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

int main(void) {
    test_push_and_stats();
    test_reject_bad_input();
    test_byte_eviction_drops_oldest();
    test_time_window_eviction();
    test_order_preserved();
    printf("%d checks, %d failures\n", g_count, g_fail);
    return g_fail ? 1 : 0;
}
