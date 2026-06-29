# esp32s3_wearcam Firmware + Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build ESP-IDF firmware for the XIAO ESP32S3 Sense that captures OV2640 video, keeps a rolling ~60s PSRAM ring buffer, live-broadcasts MJPEG over WiFi (STA), is BLE-provisioned/controllable, and serves an origin-aware vanilla-JS dashboard (also deployable to GitHub Pages).

**Architecture:** A single capture task owns the camera, pushes each JPEG into a pure-C ring buffer and publishes a "latest frame" to a frame hub. An esp_http_server exposes status/config/stream/snapshot/clip/wifi endpoints and the embedded dashboard. A NimBLE GATT service handles WiFi provisioning + control. WiFi runs STA from NVS creds, falling back to SoftAP+BLE provisioning.

**Tech Stack:** ESP-IDF (C, CMake), `espressif/esp32-camera`, `espressif/mdns`, NimBLE, esp_http_server, NVS. Dashboard: vanilla HTML/JS/CSS (no build step), Web Bluetooth. Host unit tests: C compiled natively (gcc/clang).

## Global Constraints

- Target chip: **esp32s3**. Board: XIAO ESP32S3 Sense. Flash size: **8 MB**. PSRAM: **8 MB OPI (octal)**.
- Flash/monitor serial port: **COM11** (USB-Serial-JTAG console).
- Camera defaults: **JPEG, SVGA 800×600, ~20 fps, quality 12**, runtime-tunable.
- Ring buffer bounds: **window 60000 ms** AND **byte budget 5 MB (5*1024*1024)**, whichever hits first → drop oldest. Reject any single frame larger than the byte budget.
- mDNS hostname: **wearcam.local**.
- BLE service UUID (Web-Bluetooth-discoverable, 128-bit):
  - Service `0000fe40-cc7a-482a-984a-7f2ed5b3e58f`
  - SSID (write) `0000fe41-…`, PASS (write) `0000fe42-…`, APPLY (write) `0000fe43-…`, STATUS (read/notify) `0000fe44-…`, CMD (write) `0000fe45-…` (same base suffix `-cc7a-482a-984a-7f2ed5b3e58f`).
- Ring buffer C code MUST stay free of FreeRTOS/ESP headers except behind `#ifdef RB_USE_FREERTOS`, so host unit tests compile natively.
- Directory layout: firmware in `./firmware` (ESP-IDF project root), dashboard in `./dashboard`. Dashboard files are copied into `firmware/main/www/` and embedded.

---

### Task 0: Install ESP-IDF + scaffold a booting project

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/partitions.csv`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/idf_component.yml`
- Create: `firmware/main/app_main.c`
- Create: `firmware/.gitignore`

**Interfaces:**
- Produces: a flashable IDF project; `app_main()` entry that logs a banner.

- [ ] **Step 1: Install ESP-IDF (one-time).** On Windows, install via the offline/online installer or git+install script. Verify the export script works:

```powershell
# If not present, install ESP-IDF v5.x to C:\Espressif (Espressif IDF Windows Installer)
# Then in each shell, source the exports:
& "$env:USERPROFILE\esp\esp-idf\export.ps1"   # path depends on install
idf.py --version    # expect: ESP-IDF v5.x
```
Expected: prints an ESP-IDF v5.x version string.

- [ ] **Step 2: Create `firmware/.gitignore`:**

```gitignore
build/
sdkconfig
sdkconfig.old
managed_components/
dependencies.lock
```

- [ ] **Step 3: Create `firmware/partitions.csv`:**

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 0x300000
```

- [ ] **Step 4: Create `firmware/sdkconfig.defaults`:**

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
# PSRAM (octal/OPI on XIAO ESP32S3)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP32S3_DATA_CACHE_64KB=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
# Console over native USB-Serial-JTAG
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
# Camera
CONFIG_OV2640_SUPPORT=y
# BLE NimBLE host
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
# HTTP server
CONFIG_HTTPD_MAX_REQ_HDR_LEN=2048
CONFIG_HTTPD_MAX_URI_LEN=512
# Main task stack a bit larger
CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144
```

- [ ] **Step 5: Create `firmware/CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wearcam)
```

- [ ] **Step 6: Create `firmware/main/idf_component.yml`:**

```yaml
dependencies:
  espressif/esp32-camera: "^2.0.4"
  espressif/mdns: "^1.2.0"
```

- [ ] **Step 7: Create `firmware/main/CMakeLists.txt`** (component registration; later tasks add sources + embeds):

```cmake
idf_component_register(
    SRCS
        "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        esp_timer
        mdns
        bt
        esp32-camera
)
```

- [ ] **Step 8: Create `firmware/main/app_main.c`** (minimal boot):

```c
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wearcam";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "esp32s3_wearcam boot ok");
}
```

- [ ] **Step 9: Set target and build:**

Run:
```powershell
cd firmware
idf.py set-target esp32s3
idf.py build
```
Expected: `Project build complete.` and a `wearcam.bin` under `build/`.

- [ ] **Step 10: Flash + verify boot:**

Run:
```powershell
idf.py -p COM11 flash monitor
```
Expected: serial log shows `wearcam: esp32s3_wearcam boot ok`. Press `Ctrl+]` to exit monitor.

- [ ] **Step 11: Commit:**

```bash
git add firmware/.gitignore firmware/CMakeLists.txt firmware/sdkconfig.defaults firmware/partitions.csv firmware/main/
git commit -m "feat(fw): scaffold booting ESP-IDF project for XIAO ESP32S3"
```

---

### Task 1: Ring buffer module + host unit tests (TDD)

**Files:**
- Create: `firmware/main/ring_buffer.h`
- Create: `firmware/main/ring_buffer.c`
- Create: `firmware/test_host/test_ring_buffer.c`
- Create: `firmware/test_host/Makefile`

**Interfaces:**
- Produces (consumed by capture_task + http_server):
  - `ring_buffer_t *rb_create(size_t max_bytes, int64_t window_ms);`
  - `void rb_destroy(ring_buffer_t *rb);`
  - `bool rb_push(ring_buffer_t *rb, const uint8_t *jpeg, size_t len, int64_t ts_ms);`
  - `void rb_stats(ring_buffer_t *rb, size_t *count, size_t *bytes, int64_t *span_ms);`
  - `typedef bool (*rb_iter_cb)(const uint8_t *buf, size_t len, int64_t ts_ms, void *ctx);`
  - `void rb_foreach(ring_buffer_t *rb, rb_iter_cb cb, void *ctx);`

- [ ] **Step 1: Write the header `firmware/main/ring_buffer.h`:**

```c
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
```

- [ ] **Step 2: Write the failing tests `firmware/main/../test_host/test_ring_buffer.c`** (path `firmware/test_host/test_ring_buffer.c`):

```c
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
```

- [ ] **Step 3: Write the host `firmware/test_host/Makefile`:**

```makefile
CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -I../main -g
test: test_ring_buffer
	./test_ring_buffer
test_ring_buffer: test_ring_buffer.c ../main/ring_buffer.c ../main/ring_buffer.h
	$(CC) $(CFLAGS) -o test_ring_buffer test_ring_buffer.c ../main/ring_buffer.c
clean:
	rm -f test_ring_buffer test_ring_buffer.exe
```

- [ ] **Step 4: Run the tests to verify they FAIL (no implementation yet):**

Run (use whatever host C compiler exists — `gcc`, `clang`, or `cc`; install MinGW-w64/w64devkit if none):
```bash
cd firmware/test_host && make
```
Expected: link/compile error (`ring_buffer.c`: undefined `rb_create` etc.) — FAIL.

- [ ] **Step 5: Write the implementation `firmware/main/ring_buffer.c`:**

```c
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
```

- [ ] **Step 6: Run the tests to verify they PASS:**

Run:
```bash
cd firmware/test_host && make
```
Expected: `N checks, 0 failures` and exit code 0.

- [ ] **Step 7: Wire `ring_buffer.c` into the firmware build** — edit `firmware/main/CMakeLists.txt` `SRCS` to add `"ring_buffer.c"`, and add a compile definition so the device build uses locking. Append after `idf_component_register(...)`:

```cmake
target_compile_definitions(${COMPONENT_LIB} PRIVATE RB_USE_FREERTOS=1)
```

- [ ] **Step 8: Commit:**

```bash
git add firmware/main/ring_buffer.h firmware/main/ring_buffer.c firmware/test_host/ firmware/main/CMakeLists.txt
git commit -m "feat(fw): PSRAM ring buffer with host unit tests (TDD)"
```

---

### Task 2: Camera module + capture task + frame hub

**Files:**
- Create: `firmware/main/camera.h`
- Create: `firmware/main/camera.c`
- Modify: `firmware/main/CMakeLists.txt` (add `camera.c`)
- Modify: `firmware/main/app_main.c` (start camera + capture task)

**Interfaces:**
- Produces:
  - `esp_err_t cam_init(void);`
  - `esp_err_t cam_set_config(framesize_t fs, int fps, int quality);`
  - `void cam_get_settings(int *fps, int *quality, int *width, int *height);`
  - `void cam_start_capture(ring_buffer_t *rb);` — launches the capture task.
  - `uint8_t *framehub_get(size_t *out_len);` — malloc'd copy of latest JPEG (caller frees), or NULL.
  - `bool cam_ready(void);`

- [ ] **Step 1: Write `firmware/main/camera.h`:**

```c
#pragma once
#include "esp_err.h"
#include "esp_camera.h"
#include "ring_buffer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

esp_err_t cam_init(void);
bool cam_ready(void);
esp_err_t cam_set_config(framesize_t fs, int fps, int quality);
void cam_get_settings(int *fps, int *quality, int *width, int *height);
void cam_start_capture(ring_buffer_t *rb);

// Latest published JPEG frame, malloc'd copy (caller frees). NULL if none yet.
uint8_t *framehub_get(size_t *out_len);
```

- [ ] **Step 2: Write `firmware/main/camera.c`** (XIAO ESP32S3 Sense pin map):

```c
#include "camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "camera";

// XIAO ESP32S3 Sense camera pins
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   10
#define SIOD_GPIO_NUM   40
#define SIOC_GPIO_NUM   39
#define Y9_GPIO_NUM     48
#define Y8_GPIO_NUM     11
#define Y7_GPIO_NUM     12
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     16
#define Y4_GPIO_NUM     18
#define Y3_GPIO_NUM     17
#define Y2_GPIO_NUM     15
#define VSYNC_GPIO_NUM  38
#define HREF_GPIO_NUM   47
#define PCLK_GPIO_NUM   13

static bool s_ready = false;
static int s_fps = 20;
static int s_quality = 12;
static SemaphoreHandle_t s_cfg_mtx;

// frame hub
static SemaphoreHandle_t s_hub_mtx;
static uint8_t *s_hub_buf;
static size_t   s_hub_len;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

esp_err_t cam_init(void)
{
    s_cfg_mtx = xSemaphoreCreateMutex();
    s_hub_mtx = xSemaphoreCreateMutex();

    camera_config_t cfg = {
        .pin_pwdn = PWDN_GPIO_NUM, .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM, .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM, .pin_d6 = Y8_GPIO_NUM, .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM, .pin_d3 = Y5_GPIO_NUM, .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM, .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM, .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_SVGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x", err);
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "camera ready (SVGA q12)");
    return ESP_OK;
}

bool cam_ready(void) { return s_ready; }

esp_err_t cam_set_config(framesize_t fs, int fps, int quality)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return ESP_FAIL;
    xSemaphoreTake(s_cfg_mtx, portMAX_DELAY);
    if (fps >= 1 && fps <= 30) s_fps = fps;
    if (quality >= 4 && quality <= 63) { s_quality = quality; s->set_quality(s, quality); }
    s->set_framesize(s, fs);
    xSemaphoreGive(s_cfg_mtx);
    return ESP_OK;
}

void cam_get_settings(int *fps, int *quality, int *width, int *height)
{
    sensor_t *s = esp_camera_sensor_get();
    if (fps) *fps = s_fps;
    if (quality) *quality = s_quality;
    if (s && width)  *width  = resolution[s->status.framesize].width;
    if (s && height) *height = resolution[s->status.framesize].height;
}

static void framehub_publish(const uint8_t *buf, size_t len)
{
    uint8_t *copy = malloc(len);
    if (!copy) return;
    memcpy(copy, buf, len);
    xSemaphoreTake(s_hub_mtx, portMAX_DELAY);
    free(s_hub_buf);
    s_hub_buf = copy;
    s_hub_len = len;
    xSemaphoreGive(s_hub_mtx);
}

uint8_t *framehub_get(size_t *out_len)
{
    uint8_t *copy = NULL;
    xSemaphoreTake(s_hub_mtx, portMAX_DELAY);
    if (s_hub_buf && s_hub_len) {
        copy = malloc(s_hub_len);
        if (copy) { memcpy(copy, s_hub_buf, s_hub_len); *out_len = s_hub_len; }
    }
    xSemaphoreGive(s_hub_mtx);
    return copy;
}

static void capture_task(void *arg)
{
    ring_buffer_t *rb = (ring_buffer_t *)arg;
    while (1) {
        int fps;
        xSemaphoreTake(s_cfg_mtx, portMAX_DELAY);
        fps = s_fps;
        xSemaphoreGive(s_cfg_mtx);

        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            int64_t ts = now_ms();
            rb_push(rb, fb->buf, fb->len, ts);
            framehub_publish(fb->buf, fb->len);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGW(TAG, "fb_get returned NULL");
        }
        int delay = 1000 / (fps > 0 ? fps : 20);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

void cam_start_capture(ring_buffer_t *rb)
{
    xTaskCreatePinnedToCore(capture_task, "capture", 4096, rb, 5, NULL, 1);
}
```

- [ ] **Step 3: Add `camera.c` to `firmware/main/CMakeLists.txt` `SRCS`** (list now: `app_main.c ring_buffer.c camera.c`).

- [ ] **Step 4: Update `firmware/main/app_main.c`** to init camera + ring + capture:

```c
#include "esp_log.h"
#include "nvs_flash.h"
#include "ring_buffer.h"
#include "camera.h"

static const char *TAG = "wearcam";

#define RB_MAX_BYTES (5 * 1024 * 1024)
#define RB_WINDOW_MS (60 * 1000)

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    static ring_buffer_t *rb;
    rb = rb_create(RB_MAX_BYTES, RB_WINDOW_MS);

    if (cam_init() == ESP_OK) {
        cam_start_capture(rb);
    } else {
        ESP_LOGE(TAG, "camera init failed; continuing without capture");
    }
    ESP_LOGI(TAG, "esp32s3_wearcam boot ok");
}
```

- [ ] **Step 5: Build, flash, verify camera frames:**

Run:
```powershell
cd firmware
idf.py build
idf.py -p COM11 flash monitor
```
Expected: log `camera ready (SVGA q12)` and no repeated `fb_get returned NULL`. (If `fb_get NULL` floods, the camera/ribbon is unseated — stop and report.)

- [ ] **Step 6: Commit:**

```bash
git add firmware/main/camera.h firmware/main/camera.c firmware/main/CMakeLists.txt firmware/main/app_main.c
git commit -m "feat(fw): OV2640 camera, capture task, frame hub"
```

---

### Task 3: WiFi STA + NVS credentials + provisioning-mode signal

**Files:**
- Create: `firmware/main/wearcam_wifi.h`
- Create: `firmware/main/wearcam_wifi.c`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/app_main.c`

**Interfaces:**
- Produces:
  - `void wifi_start(void);` — reads NVS creds; STA if present else SoftAP provisioning.
  - `bool wifi_set_credentials(const char *ssid, const char *pass);` — save to NVS + reconnect.
  - `bool wifi_is_connected(void);`
  - `void wifi_get_ip(char *out, size_t n);` — "0.0.0.0" if not connected.
  - `int wifi_get_rssi(void);`
  - `bool wifi_is_provisioning(void);`
- NVS namespace `"wearcam"`, keys `"ssid"`, `"pass"`.

- [ ] **Step 1: Write `firmware/main/wearcam_wifi.h`:**

```c
#pragma once
#include <stdbool.h>
#include <stddef.h>

void wifi_start(void);
bool wifi_set_credentials(const char *ssid, const char *pass);
bool wifi_is_connected(void);
bool wifi_is_provisioning(void);
void wifi_get_ip(char *out, size_t n);
int  wifi_get_rssi(void);
```

- [ ] **Step 2: Write `firmware/main/wearcam_wifi.c`:**

```c
#include "wearcam_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";
#define NVS_NS "wearcam"
#define MAX_RETRY 8

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static EventGroupHandle_t s_eg;
#define CONNECTED_BIT BIT0
static int s_retry;
static bool s_provisioning;
static char s_ip[16] = "0.0.0.0";

static bool load_creds(char *ssid, size_t sn, char *pass, size_t pn)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_str(h, "ssid", ssid, &sn) == ESP_OK &&
              nvs_get_str(h, "pass", pass, &pn) == ESP_OK &&
              strlen(ssid) > 0;
    nvs_close(h);
    return ok;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_eg, CONNECTED_BIT);
        strcpy(s_ip, "0.0.0.0");
        if (s_retry < MAX_RETRY) {
            s_retry++;
            vTaskDelay(pdMS_TO_TICKS(500 * s_retry));
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "STA failed after retries; staying down");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_eg, CONNECTED_BIT);
        ESP_LOGI(TAG, "STA got IP %s", s_ip);
    }
}

static void start_sta(const char *ssid, const char *pass)
{
    s_provisioning = false;
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "STA connecting to '%s'", ssid);
}

static void start_ap(void)
{
    s_provisioning = true;
    wifi_config_t wc = {0};
    strcpy((char *)wc.ap.ssid, "wearcam-setup");
    wc.ap.ssid_len = strlen("wearcam-setup");
    wc.ap.channel = 1;
    wc.ap.max_connection = 2;
    wc.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    strcpy(s_ip, "192.168.4.1");
    ESP_LOGI(TAG, "provisioning AP 'wearcam-setup' up (192.168.4.1)");
}

void wifi_start(void)
{
    s_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));

    char ssid[33] = {0}, pass[65] = {0};
    if (load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        start_sta(ssid, pass);
    } else {
        start_ap();
    }
}

bool wifi_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || strlen(ssid) == 0) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass ? pass : "");
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return false;

    // restart wifi as STA with new creds
    esp_wifi_stop();
    s_retry = 0;
    start_sta(ssid, pass);
    return true;
}

bool wifi_is_connected(void) { return (xEventGroupGetBits(s_eg) & CONNECTED_BIT) != 0; }
bool wifi_is_provisioning(void) { return s_provisioning; }
void wifi_get_ip(char *out, size_t n) { strlcpy(out, s_ip, n); }

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}
```

- [ ] **Step 3: Add `wearcam_wifi.c` to `firmware/main/CMakeLists.txt` `SRCS`.**

- [ ] **Step 4: Call `wifi_start()` in `app_main.c`** — add `#include "wearcam_wifi.h"` and call `wifi_start();` after `nvs_flash_init()` block, before camera init.

- [ ] **Step 5: Build + flash + verify provisioning AP** (no creds yet):

Run:
```powershell
cd firmware && idf.py build && idf.py -p COM11 flash monitor
```
Expected: log `provisioning AP 'wearcam-setup' up (192.168.4.1)`. On a phone/PC, the `wearcam-setup` SSID is visible.

- [ ] **Step 6: Commit:**

```bash
git add firmware/main/wearcam_wifi.h firmware/main/wearcam_wifi.c firmware/main/CMakeLists.txt firmware/main/app_main.c
git commit -m "feat(fw): WiFi STA from NVS with SoftAP provisioning fallback"
```

---

### Task 4: Dashboard SPA (vanilla JS, origin-aware)

**Files:**
- Create: `dashboard/index.html`
- Create: `dashboard/app.js`
- Create: `dashboard/style.css`
- Create: `dashboard/README.md`

**Interfaces:**
- Produces static files later copied to `firmware/main/www/` and embedded. The HTTP API it calls (`/api/status`, `/api/config`, `/api/wifi`, `/stream`, `/snapshot`, `/clip`) is implemented in Task 6; the BLE UUIDs match Task 5.

- [ ] **Step 1: Write `dashboard/index.html`:**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>WearCam</title>
  <link rel="stylesheet" href="style.css" />
</head>
<body>
  <header><h1>WearCam</h1><span id="conn" class="pill">…</span></header>

  <section id="ble-panel" hidden>
    <h2>Setup (Bluetooth)</h2>
    <p class="hint">Hosted page — provision WiFi over Bluetooth, then open the device.</p>
    <button id="ble-connect">Connect device</button>
    <form id="wifi-form" hidden>
      <input id="ssid" placeholder="WiFi SSID" autocomplete="off" />
      <input id="pass" placeholder="WiFi password" type="password" />
      <button type="submit">Send credentials</button>
    </form>
    <p id="ble-status"></p>
    <a id="open-device" hidden>Open device dashboard →</a>
  </section>

  <section id="live-panel" hidden>
    <div class="video-wrap"><img id="stream" alt="live stream" /></div>
    <div class="controls">
      <button id="snap">Snapshot</button>
      <button id="clip">Download last 60s</button>
      <button id="toggle">Pause</button>
    </div>
    <form id="config-form" class="grid">
      <label>Resolution
        <select id="res">
          <option value="QVGA">QVGA 320×240</option>
          <option value="VGA">VGA 640×480</option>
          <option value="SVGA" selected>SVGA 800×600</option>
          <option value="HD">HD 1280×720</option>
        </select>
      </label>
      <label>FPS <input id="fps" type="number" min="1" max="30" value="20" /></label>
      <label>Quality <input id="quality" type="number" min="4" max="63" value="12" /></label>
      <button type="submit">Apply</button>
    </form>
    <pre id="status" class="status"></pre>
  </section>

  <script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 2: Write `dashboard/style.css`:**

```css
:root { --bg:#10131a; --fg:#e6edf3; --accent:#3fb950; --panel:#1b2130; }
* { box-sizing: border-box; }
body { margin:0; font-family: system-ui, sans-serif; background:var(--bg); color:var(--fg); }
header { display:flex; align-items:center; gap:.75rem; padding:1rem; border-bottom:1px solid #2a3140; }
h1 { font-size:1.25rem; margin:0; }
.pill { font-size:.75rem; padding:.15rem .5rem; border-radius:1rem; background:var(--panel); }
.pill.ok { background:var(--accent); color:#06210c; }
section { padding:1rem; max-width:760px; margin:0 auto; }
.video-wrap { background:#000; border-radius:8px; overflow:hidden; aspect-ratio:4/3; display:flex; }
#stream { width:100%; height:100%; object-fit:contain; }
.controls { display:flex; gap:.5rem; margin:.75rem 0; flex-wrap:wrap; }
button, .controls a { background:var(--panel); color:var(--fg); border:1px solid #2a3140; padding:.5rem .9rem; border-radius:6px; cursor:pointer; }
button:hover { border-color:var(--accent); }
.grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(140px,1fr)); gap:.75rem; align-items:end; }
label { display:flex; flex-direction:column; font-size:.8rem; gap:.25rem; }
input, select { background:#0c0f16; color:var(--fg); border:1px solid #2a3140; border-radius:6px; padding:.4rem; }
.status { background:#0c0f16; padding:.75rem; border-radius:6px; font-size:.75rem; white-space:pre-wrap; }
.hint { font-size:.8rem; opacity:.7; }
a { color:var(--accent); }
```

- [ ] **Step 3: Write `dashboard/app.js`:**

```js
// Origin-aware: served from device (http) => live panel; hosted (https) => BLE setup.
const SVC  = '0000fe40-cc7a-482a-984a-7f2ed5b3e58f';
const C_SSID  = '0000fe41-cc7a-482a-984a-7f2ed5b3e58f';
const C_PASS  = '0000fe42-cc7a-482a-984a-7f2ed5b3e58f';
const C_APPLY = '0000fe43-cc7a-482a-984a-7f2ed5b3e58f';
const C_STAT  = '0000fe44-cc7a-482a-984a-7f2ed5b3e58f';

const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();
// Device serves this file over http(s) from its own origin (not github.io).
const onDevice = !location.hostname.endsWith('github.io') && location.protocol.startsWith('http');

function setConn(text, ok) { const c = $('conn'); c.textContent = text; c.classList.toggle('ok', !!ok); }

if (onDevice) initLive(); else initBle();

// ---------- Device (HTTP) mode ----------
function initLive() {
  $('live-panel').hidden = false;
  let streaming = true;
  const img = $('stream');
  img.src = '/stream';

  $('toggle').onclick = () => {
    streaming = !streaming;
    img.src = streaming ? '/stream' : '';
    $('toggle').textContent = streaming ? 'Pause' : 'Resume';
  };
  $('snap').onclick = () => window.open('/snapshot', '_blank');
  $('clip').onclick = () => window.open('/clip', '_blank');

  $('config-form').onsubmit = async (e) => {
    e.preventDefault();
    const body = JSON.stringify({
      res: $('res').value, fps: +$('fps').value, quality: +$('quality').value });
    await fetch('/api/config', { method:'POST', headers:{'Content-Type':'application/json'}, body });
  };

  async function poll() {
    try {
      const r = await fetch('/api/status');
      const s = await r.json();
      $('status').textContent = JSON.stringify(s, null, 2);
      setConn(`${s.ip} · ${s.rssi}dBm`, true);
    } catch { setConn('offline', false); }
    setTimeout(poll, 2000);
  }
  poll();
}

// ---------- Hosted (BLE) mode ----------
function initBle() {
  $('ble-panel').hidden = false;
  let chars = {};

  $('ble-connect').onclick = async () => {
    try {
      const dev = await navigator.bluetooth.requestDevice({ filters:[{ services:[SVC] }] });
      const gatt = await dev.gatt.connect();
      const svc = await gatt.getPrimaryService(SVC);
      chars.ssid  = await svc.getCharacteristic(C_SSID);
      chars.pass  = await svc.getCharacteristic(C_PASS);
      chars.apply = await svc.getCharacteristic(C_APPLY);
      chars.stat  = await svc.getCharacteristic(C_STAT);
      await chars.stat.startNotifications();
      chars.stat.addEventListener('characteristicvaluechanged', (e) => {
        const txt = new TextDecoder().decode(e.target.value);
        $('ble-status').textContent = txt;
        const m = txt.match(/(\d+\.\d+\.\d+\.\d+)/);
        if (m && m[1] !== '0.0.0.0') {
          const a = $('open-device');
          a.hidden = false; a.href = `http://${m[1]}/`;
          a.textContent = `Open device dashboard (http://${m[1]}) →`;
        }
      });
      $('wifi-form').hidden = false;
      setConn('BLE connected', true);
    } catch (err) { $('ble-status').textContent = 'BLE error: ' + err.message; }
  };

  $('wifi-form').onsubmit = async (e) => {
    e.preventDefault();
    await chars.ssid.writeValue(enc.encode($('ssid').value));
    await chars.pass.writeValue(enc.encode($('pass').value));
    await chars.apply.writeValue(Uint8Array.of(1));
    $('ble-status').textContent = 'Credentials sent; device joining WiFi…';
  };
}
```

- [ ] **Step 4: Write `dashboard/README.md`:**

```markdown
# WearCam Dashboard

Single-page, no build step. Two roles from one codebase:

- **Hosted (GitHub Pages, https):** Web Bluetooth WiFi provisioning. Connect the
  device, send SSID/password, then follow the "Open device" link.
- **On-device (http, served by firmware):** live MJPEG stream, snapshot,
  download-last-60s, and resolution/fps/quality config.

These files are also copied into `firmware/main/www/` and embedded into the
firmware, so the device serves an identical UI offline.

Deploy: push to `main`, enable GitHub Pages on `/dashboard` (or copy to `/docs`).
```

- [ ] **Step 5: Verify HTML/JS parse** (no device needed) with Node:

Run:
```powershell
node --check dashboard/app.js
```
Expected: no output, exit 0 (syntax OK).

- [ ] **Step 6: Commit:**

```bash
git add dashboard/
git commit -m "feat(dashboard): origin-aware vanilla SPA (BLE provisioning + live view)"
```

---

### Task 5: BLE provisioning + control (NimBLE GATT)

**Files:**
- Create: `firmware/main/ble_prov.h`
- Create: `firmware/main/ble_prov.c`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/app_main.c`

**Interfaces:**
- Consumes: `wifi_set_credentials()`, `wifi_get_ip()`, `wifi_is_connected()` (Task 3); `cam_set_config()` (Task 2 — for CMD).
- Produces: `void ble_prov_start(void);`
- UUIDs exactly as in Global Constraints; STATUS characteristic notifies a short string `state ip=<ip>`.

- [ ] **Step 1: Write `firmware/main/ble_prov.h`:**

```c
#pragma once
void ble_prov_start(void);
```

- [ ] **Step 2: Write `firmware/main/ble_prov.c`:**

```c
#include "ble_prov.h"
#include "wearcam_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "ble";
static uint8_t s_addr_type;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_stat_attr;

static char s_ssid[33];
static char s_pass[65];

// 128-bit base: xxxx fe4N -cc7a-482a-984a-7f2ed5b3e58f
#define DECL_UUID(name, b2, b3) \
  static const ble_uuid128_t name = BLE_UUID128_INIT( \
    0x8f,0xe5,0xb3,0xd5,0x2e,0x7f,0x4a,0x98,0x2a,0x48,0x7a,0xcc, b3, b2, 0x00,0x00)
DECL_UUID(UUID_SVC,   0x40, 0xfe);
DECL_UUID(UUID_SSID,  0x41, 0xfe);
DECL_UUID(UUID_PASS,  0x42, 0xfe);
DECL_UUID(UUID_APPLY, 0x43, 0xfe);
DECL_UUID(UUID_STAT,  0x44, 0xfe);
DECL_UUID(UUID_CMD,   0x45, 0xfe);

static void notify_status(void)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) return;
    char ip[16]; wifi_get_ip(ip, sizeof(ip));
    char msg[48];
    int n = snprintf(msg, sizeof(msg), "%s ip=%s",
                     wifi_is_connected() ? "connected" : "setup", ip);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, n);
    ble_gatts_notify_custom(s_conn, s_stat_attr, om);
}

static int chr_access(uint16_t ch, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *u = ctxt->chr->uuid;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[65] = {0};
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
        if (ble_uuid_cmp(u, &UUID_SSID.u) == 0) {
            strlcpy(s_ssid, buf, sizeof(s_ssid));
        } else if (ble_uuid_cmp(u, &UUID_PASS.u) == 0) {
            strlcpy(s_pass, buf, sizeof(s_pass));
        } else if (ble_uuid_cmp(u, &UUID_APPLY.u) == 0) {
            ESP_LOGI(TAG, "apply creds ssid='%s'", s_ssid);
            wifi_set_credentials(s_ssid, s_pass);
            notify_status();
        } else if (ble_uuid_cmp(u, &UUID_CMD.u) == 0) {
            ESP_LOGI(TAG, "cmd: %s", buf);   // start/stop/snapshot hooks
        }
        return 0;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char ip[16]; wifi_get_ip(ip, sizeof(ip));
        char msg[48];
        int n = snprintf(msg, sizeof(msg), "%s ip=%s",
                         wifi_is_connected() ? "connected" : "setup", ip);
        os_mbuf_append(ctxt->om, msg, n);
        return 0;
    }
    (void)ch; (void)attr; (void)arg;
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &UUID_SSID.u,  .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &UUID_PASS.u,  .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &UUID_APPLY.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &UUID_CMD.u,   .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &UUID_STAT.u,  .access_cb = chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_stat_attr },
            { 0 },
        },
    },
    { 0 },
};

static void advertise(void)
{
    struct ble_gap_adv_params adv = {0};
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)"WearCam"; f.name_len = 7; f.name_is_complete = 1;
    f.uuids128 = (ble_uuid128_t *)&UUID_SVC; f.num_uuids128 = 1; f.uuids128_is_complete = 1;
    ble_gap_adv_set_fields(&f);
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, NULL, NULL);
    ESP_LOGI(TAG, "advertising as WearCam");
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) { s_conn = ev->connect.conn_handle; notify_status(); }
        else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE; advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        notify_status();
        break;
    }
    (void)arg;
    return 0;
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_addr_type);
    advertise();
}

static void host_task(void *param) { nimble_port_run(); nimble_port_freertos_deinit(); }

void ble_prov_start(void)
{
    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(s_svcs);
    ble_gatts_add_svcs(s_svcs);
    ble_svc_gap_device_name_set("WearCam");
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "nimble started");
    // store gap_event for connect handling
    (void)gap_event;
}
```

> Note: `advertise()` must pass `gap_event` as its callback. Fix the `ble_gap_adv_start` call to `ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);` and remove the trailing `(void)gap_event;`.

- [ ] **Step 3: Apply the note** — in `firmware/main/ble_prov.c`, change the `ble_gap_adv_start(...)` line inside `advertise()` to use `gap_event` as the cb, and delete `(void)gap_event;` in `ble_prov_start()`:

```c
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
```

- [ ] **Step 4: Add `ble_prov.c` to `firmware/main/CMakeLists.txt` `SRCS`.**

- [ ] **Step 5: Call `ble_prov_start()` in `app_main.c`** — add `#include "ble_prov.h"` and call `ble_prov_start();` after `wifi_start();`.

- [ ] **Step 6: Build + flash + verify BLE advertises:**

Run:
```powershell
cd firmware && idf.py build && idf.py -p COM11 flash monitor
```
Expected: logs `nimble started` then `advertising as WearCam`. On a phone (nRF Connect) or Chrome, a **WearCam** BLE device with service `…fe40…` is visible.

- [ ] **Step 7: End-to-end provisioning test** — open the hosted dashboard (or `chrome://bluetooth-internals`), connect WearCam, send real SSID/pass, click apply. Expected serial: `apply creds ssid='…'` then `STA got IP <x>`; STATUS notifies `connected ip=<x>`.

- [ ] **Step 8: Commit:**

```bash
git add firmware/main/ble_prov.h firmware/main/ble_prov.c firmware/main/CMakeLists.txt firmware/main/app_main.c
git commit -m "feat(fw): NimBLE WiFi provisioning + control GATT service"
```

---

### Task 6: HTTP server (REST + streaming) + embedded dashboard + mDNS

**Files:**
- Create: `firmware/main/http_server.h`
- Create: `firmware/main/http_server.c`
- Create: `firmware/main/www/` (copied from `dashboard/`)
- Modify: `firmware/main/CMakeLists.txt` (sources + `EMBED_FILES`)
- Modify: `firmware/main/app_main.c`

**Interfaces:**
- Consumes: `framehub_get()`, `cam_set_config()`, `cam_get_settings()`, `cam_ready()` (Task 2); `wifi_get_ip()`, `wifi_get_rssi()`, `wifi_is_provisioning()` (Task 3); `rb_foreach()` (Task 1, via a global ring pointer passed in).
- Produces: `void http_start(ring_buffer_t *rb);` and `void wearcam_mdns_start(void);`

- [ ] **Step 1: Copy dashboard into the firmware embed dir:**

Run:
```bash
mkdir -p firmware/main/www
cp dashboard/index.html dashboard/app.js dashboard/style.css firmware/main/www/
```

- [ ] **Step 2: Write `firmware/main/http_server.h`:**

```c
#pragma once
#include "ring_buffer.h"
void http_start(ring_buffer_t *rb);
void wearcam_mdns_start(void);
```

- [ ] **Step 3: Write `firmware/main/http_server.c`:**

```c
#include "http_server.h"
#include "camera.h"
#include "wearcam_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http";
static ring_buffer_t *s_rb;

// Embedded dashboard (see CMakeLists EMBED_FILES)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t app_js_start[]      asm("_binary_app_js_start");
extern const uint8_t app_js_end[]        asm("_binary_app_js_end");
extern const uint8_t style_css_start[]   asm("_binary_style_css_start");
extern const uint8_t style_css_end[]     asm("_binary_style_css_end");

#define PART_BOUNDARY "wearcamframe"
static const char *STREAM_CT = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *PART_HDR = "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t send_static(httpd_req_t *r, const char *ct, const uint8_t *s, const uint8_t *e)
{
    httpd_resp_set_type(r, ct);
    return httpd_resp_send(r, (const char *)s, e - s);
}

static esp_err_t h_index(httpd_req_t *r){ return send_static(r,"text/html",index_html_start,index_html_end); }
static esp_err_t h_appjs(httpd_req_t *r){ return send_static(r,"application/javascript",app_js_start,app_js_end); }
static esp_err_t h_css(httpd_req_t *r){ return send_static(r,"text/css",style_css_start,style_css_end); }

static esp_err_t h_snapshot(httpd_req_t *r)
{
    size_t len = 0;
    uint8_t *jpg = framehub_get(&len);
    if (!jpg) { httpd_resp_send_500(r); return ESP_FAIL; }
    httpd_resp_set_type(r, "image/jpeg");
    httpd_resp_set_hdr(r, "Content-Disposition", "inline; filename=snap.jpg");
    esp_err_t res = httpd_resp_send(r, (const char *)jpg, len);
    free(jpg);
    return res;
}

static esp_err_t h_stream(httpd_req_t *r)
{
    httpd_resp_set_type(r, STREAM_CT);
    char hdr[80];
    while (true) {
        size_t len = 0;
        uint8_t *jpg = framehub_get(&len);
        if (jpg) {
            int n = snprintf(hdr, sizeof(hdr), PART_HDR, (unsigned)len);
            if (httpd_resp_send_chunk(r, hdr, n) != ESP_OK ||
                httpd_resp_send_chunk(r, (const char *)jpg, len) != ESP_OK) {
                free(jpg); break;
            }
            free(jpg);
        }
        int fps; cam_get_settings(&fps, NULL, NULL, NULL);
        vTaskDelay(pdMS_TO_TICKS(1000 / (fps > 0 ? fps : 20)));
    }
    return ESP_OK;
}

static bool clip_cb(const uint8_t *buf, size_t len, int64_t ts, void *ctx)
{
    httpd_req_t *r = (httpd_req_t *)ctx;
    char hdr[80];
    int n = snprintf(hdr, sizeof(hdr), PART_HDR, (unsigned)len);
    if (httpd_resp_send_chunk(r, hdr, n) != ESP_OK) return false;
    if (httpd_resp_send_chunk(r, (const char *)buf, len) != ESP_OK) return false;
    (void)ts;
    return true;
}

static esp_err_t h_clip(httpd_req_t *r)
{
    httpd_resp_set_type(r, STREAM_CT);
    rb_foreach(s_rb, clip_cb, r);
    httpd_resp_send_chunk(r, NULL, 0);   // end
    return ESP_OK;
}

static esp_err_t h_status(httpd_req_t *r)
{
    char ip[16]; wifi_get_ip(ip, sizeof(ip));
    int fps, q, w, h; cam_get_settings(&fps, &q, &w, &h);
    size_t cnt = 0, bytes = 0; int64_t span = 0;
    if (s_rb) rb_stats(s_rb, &cnt, &bytes, &span);
    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"ip\":\"%s\",\"rssi\":%d,\"fps\":%d,\"quality\":%d,\"w\":%d,\"h\":%d,"
        "\"cam\":%s,\"buf_frames\":%u,\"buf_bytes\":%u,\"buf_span_ms\":%lld,"
        "\"heap\":%u,\"provisioning\":%s}",
        ip, wifi_get_rssi(), fps, q, w, h, cam_ready() ? "true" : "false",
        (unsigned)cnt, (unsigned)bytes, (long long)span,
        (unsigned)esp_get_free_heap_size(), wifi_is_provisioning() ? "true" : "false");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

static framesize_t parse_res(const char *s)
{
    if (!strcmp(s, "QVGA")) return FRAMESIZE_QVGA;
    if (!strcmp(s, "VGA"))  return FRAMESIZE_VGA;
    if (!strcmp(s, "HD"))   return FRAMESIZE_HD;
    return FRAMESIZE_SVGA;
}

// tiny helpers to pull a value out of flat JSON without a parser
static bool json_str(const char *body, const char *key, char *out, size_t n)
{
    char pat[24]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(body, pat); if (!p) return false;
    p = strchr(p + strlen(pat), ':'); if (!p) return false;
    p = strchr(p, '"'); if (!p) return false; p++;
    const char *e = strchr(p, '"'); if (!e) return false;
    size_t l = (size_t)(e - p); if (l >= n) l = n - 1;
    memcpy(out, p, l); out[l] = 0; return true;
}
static bool json_int(const char *body, const char *key, int *out)
{
    char pat[24]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(body, pat); if (!p) return false;
    p = strchr(p + strlen(pat), ':'); if (!p) return false;
    *out = atoi(p + 1); return true;
}

static esp_err_t h_config(httpd_req_t *r)
{
    char body[256]; int len = httpd_req_recv(r, body, sizeof(body) - 1);
    if (len <= 0) { httpd_resp_send_500(r); return ESP_FAIL; }
    body[len] = 0;
    char res[8] = "SVGA"; int fps = 20, q = 12;
    json_str(body, "res", res, sizeof(res));
    json_int(body, "fps", &fps);
    json_int(body, "quality", &q);
    cam_set_config(parse_res(res), fps, q);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, "{\"ok\":true}", 11);
}

static esp_err_t h_wifi_post(httpd_req_t *r)
{
    char body[160]; int len = httpd_req_recv(r, body, sizeof(body) - 1);
    if (len <= 0) { httpd_resp_send_500(r); return ESP_FAIL; }
    body[len] = 0;
    char ssid[33] = {0}, pass[65] = {0};
    json_str(body, "ssid", ssid, sizeof(ssid));
    json_str(body, "pass", pass, sizeof(pass));
    bool ok = wifi_set_credentials(ssid, pass);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, ok ? "{\"ok\":true}" : "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_wifi_get(httpd_req_t *r)
{
    char ip[16]; wifi_get_ip(ip, sizeof(ip));
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"ip\":\"%s\",\"connected\":%s}",
                     ip, wifi_is_connected() ? "true" : "false");
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

static void reg(httpd_handle_t s, const char *uri, httpd_method_t m, esp_err_t(*h)(httpd_req_t*))
{
    httpd_uri_t u = { .uri = uri, .method = m, .handler = h };
    httpd_register_uri_handler(s, &u);
}

void http_start(ring_buffer_t *rb)
{
    s_rb = rb;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return; }
    reg(srv, "/",          HTTP_GET,  h_index);
    reg(srv, "/app.js",    HTTP_GET,  h_appjs);
    reg(srv, "/style.css", HTTP_GET,  h_css);
    reg(srv, "/snapshot",  HTTP_GET,  h_snapshot);
    reg(srv, "/stream",    HTTP_GET,  h_stream);
    reg(srv, "/clip",      HTTP_GET,  h_clip);
    reg(srv, "/api/status",HTTP_GET,  h_status);
    reg(srv, "/api/config",HTTP_POST, h_config);
    reg(srv, "/api/wifi",  HTTP_GET,  h_wifi_get);
    reg(srv, "/api/wifi",  HTTP_POST, h_wifi_post);
    ESP_LOGI(TAG, "http server up");
}

void wearcam_mdns_start(void)
{
    if (mdns_init() != ESP_OK) return;
    mdns_hostname_set("wearcam");
    mdns_instance_name_set("WearCam");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mdns: wearcam.local");
}
```

- [ ] **Step 4: Update `firmware/main/CMakeLists.txt`** — add `http_server.c` to `SRCS` and append the embeds. Final file:

```cmake
idf_component_register(
    SRCS
        "app_main.c"
        "ring_buffer.c"
        "camera.c"
        "wearcam_wifi.c"
        "ble_prov.c"
        "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash esp_wifi esp_netif esp_event esp_http_server
        esp_timer mdns bt esp32-camera
    EMBED_FILES
        "www/index.html"
        "www/app.js"
        "www/style.css"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE RB_USE_FREERTOS=1)
```

- [ ] **Step 5: Start HTTP + mDNS in `app_main.c`** — add includes `#include "http_server.h"`, keep the `rb` pointer, and after camera/wifi/ble init add:

```c
    http_start(rb);
    wearcam_mdns_start();
```

- [ ] **Step 6: Build + flash:**

Run:
```powershell
cd firmware && idf.py build && idf.py -p COM11 flash monitor
```
Expected: logs `http server up` and `mdns: wearcam.local`. Note the STA IP from the log.

- [ ] **Step 7: Verify endpoints over LAN** (device must be STA-connected — provision first if needed). Replace `<IP>`:

Run:
```powershell
curl http://<IP>/api/status
curl -o snap.jpg http://<IP>/snapshot
```
Expected: `/api/status` returns JSON with `"cam":true` and `buf_frames` > 0; `snap.jpg` is a valid JPEG (>2 KB). Open `http://<IP>/` in a browser → live stream renders.

- [ ] **Step 8: Commit:**

```bash
git add firmware/main/http_server.h firmware/main/http_server.c firmware/main/www/ firmware/main/CMakeLists.txt firmware/main/app_main.c
git commit -m "feat(fw): HTTP server (stream/snapshot/clip/status/config/wifi), embedded dashboard, mDNS"
```

---

### Task 7: End-to-end smoke verification, README, GitHub Pages

**Files:**
- Modify: `README.md`
- Create: `.github/workflows/pages.yml` (optional GH Pages deploy of `dashboard/`)
- Create: `docs/SMOKE.md` (verification checklist + results)

**Interfaces:**
- Consumes: the running firmware from Tasks 0–6.

- [ ] **Step 1: Run the full on-device smoke checklist and record results in `docs/SMOKE.md`:**

```markdown
# Smoke verification — <date>

Device: XIAO ESP32S3 Sense @ COM11, firmware <git short sha>.

- [ ] Boots: serial shows `camera ready`, `http server up`, `mdns: wearcam.local`.
- [ ] No creds → `provisioning AP 'wearcam-setup'` + BLE `advertising as WearCam`.
- [ ] BLE provisioning sets creds → `STA got IP <ip>`; STATUS notifies `connected ip=<ip>`.
- [ ] `GET http://<ip>/api/status` → 200, `"cam":true`, `buf_frames>0`.
- [ ] `GET http://<ip>/snapshot` → valid JPEG.
- [ ] `GET http://<ip>/stream` → live MJPEG in browser.
- [ ] `GET http://<ip>/clip` → downloads multipart MJPEG of buffered frames.
- [ ] `POST http://<ip>/api/config {"res":"VGA","fps":15,"quality":15}` → stream resolution changes.
- [ ] `http://wearcam.local/` resolves on the LAN.

Results / notes:
```

- [ ] **Step 2: Update `README.md`** — add Build/Flash + Dashboard sections:

```markdown
## Build & Flash (firmware)

Requires ESP-IDF v5.x.

```bash
cd firmware
idf.py set-target esp32s3      # first time only
idf.py build
idf.py -p COM11 flash monitor  # adjust port
```

First boot has no WiFi credentials, so the device starts a `wearcam-setup`
SoftAP and advertises a **WearCam** BLE service. Open the dashboard
(GitHub Pages) on a Web-Bluetooth browser, connect, and send your WiFi
SSID/password. The device reboots into STA mode and is reachable at
`http://wearcam.local` (or its DHCP IP).

## Dashboard

`dashboard/` is a no-build vanilla SPA. Hosted on GitHub Pages it provisions
WiFi over Bluetooth; served from the device (`http://wearcam.local`) it shows
the live stream, snapshots, the last-60s clip download, and camera config.
The same files are embedded into the firmware.
```

- [ ] **Step 3: (Optional) Create `.github/workflows/pages.yml`** to publish `dashboard/`:

```yaml
name: Deploy dashboard to Pages
on:
  push:
    branches: [main]
permissions:
  contents: read
  pages: write
  id-token: write
jobs:
  deploy:
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/configure-pages@v5
      - uses: actions/upload-pages-artifact@v3
        with:
          path: dashboard
      - id: deployment
        uses: actions/deploy-pages@v4
```

- [ ] **Step 4: Re-run host unit tests (regression):**

Run:
```bash
cd firmware/test_host && make
```
Expected: `N checks, 0 failures`.

- [ ] **Step 5: Commit:**

```bash
git add README.md docs/SMOKE.md .github/workflows/pages.yml
git commit -m "docs: build/flash + dashboard README, smoke checklist, Pages deploy"
```

---

## Self-Review notes

- **Spec coverage:** auto-record rolling 60s → Task 1 (ring) + Task 2 (capture). Wireless broadcast → Task 6 (`/stream`). BLE control → Task 5. GH-Pages dashboard + device preview → Task 4 + Task 6 embed. WiFi STA + provisioning → Task 3 + Task 5. mDNS → Task 6. Host TDD → Task 1. Build/flash/verify → Tasks 0–7.
- **Embed asset names:** IDF derives symbols from filenames: `index.html`→`_binary_index_html_*`, `app.js`→`_binary_app_js_*`, `style.css`→`_binary_style_css_*`. Matches `http_server.c`.
- **Type consistency:** `framehub_get(size_t*)`, `cam_get_settings(int*,int*,int*,int*)`, `rb_foreach(rb, rb_iter_cb, ctx)`, `wifi_set_credentials(ssid,pass)` used identically across tasks.
- **Known risk:** host C compiler may be absent on Windows — Task 1 Step 4 notes installing MinGW-w64/w64devkit. ESP-IDF install (Task 0) is a prerequisite for all device steps.
```

