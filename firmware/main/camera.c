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

esp_err_t wearcam_cam_init(void)
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
