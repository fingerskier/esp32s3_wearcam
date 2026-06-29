#include "esp_log.h"
#include "nvs_flash.h"
#include "ring_buffer.h"
#include "camera.h"
#include "wearcam_wifi.h"
#include "ble_prov.h"
#include "http_server.h"

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

    wifi_start();
    ble_prov_start();

    static ring_buffer_t *rb;
    rb = rb_create(RB_MAX_BYTES, RB_WINDOW_MS);

    if (wearcam_cam_init() == ESP_OK) {
        cam_start_capture(rb);
    } else {
        ESP_LOGE(TAG, "camera init failed; continuing without capture");
    }

    http_start(rb);
    wearcam_mdns_start();

    ESP_LOGI(TAG, "esp32s3_wearcam boot ok");
}
