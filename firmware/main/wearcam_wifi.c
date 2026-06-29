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
static wifi_ip_cb_t s_ip_cb;

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
        if (s_ip_cb) s_ip_cb();
    }
}

static esp_err_t start_sta(const char *ssid, const char *pass)
{
    s_provisioning = false;
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    esp_err_t err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_STA, &wc)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    ESP_LOGI(TAG, "STA connecting to '%s'", ssid);
    return ESP_OK;
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
        if (start_sta(ssid, pass) != ESP_OK) ESP_LOGE(TAG, "STA start failed at boot");
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

    // restart wifi as STA with new creds. Called from a worker task (not the
    // BLE host callback); errors are returned, never aborted.
    esp_wifi_stop();
    s_retry = 0;
    if (start_sta(ssid, pass) != ESP_OK) {
        ESP_LOGE(TAG, "STA restart failed applying new creds");
        return false;
    }
    return true;
}

bool wifi_is_connected(void) { return (xEventGroupGetBits(s_eg) & CONNECTED_BIT) != 0; }
bool wifi_is_provisioning(void) { return s_provisioning; }
void wifi_get_ip(char *out, size_t n) { strlcpy(out, s_ip, n); }
void wifi_set_ip_cb(wifi_ip_cb_t cb) { s_ip_cb = cb; }

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}
