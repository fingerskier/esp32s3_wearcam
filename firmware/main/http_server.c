#include "http_server.h"
#include "camera.h"
#include "wearcam_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

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

#define STREAM_PORT 81                 // /stream runs on its own httpd instance
#define CLIP_FRAME_CAP (256 * 1024)    // max single JPEG copied per clip frame

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
    httpd_resp_send_chunk(r, NULL, 0);   // terminate the multipart stream
    return ESP_OK;
}

static esp_err_t h_clip(httpd_req_t *r)
{
    httpd_resp_set_type(r, STREAM_CT);
    uint8_t *tmp = malloc(CLIP_FRAME_CAP);
    if (!tmp) { httpd_resp_send_500(r); return ESP_FAIL; }
    // Snapshot the upper time bound so a slow client can't stream forever as
    // new frames keep arriving. rb_copy_next copies each frame out under the
    // ring lock; we send it with the lock released, so capture/rb_push never
    // stalls for the duration of the download.
    int64_t end_ts = rb_newest_ts(s_rb, INT64_MIN);
    int64_t after = INT64_MIN;
    size_t flen = 0; int64_t fts = 0;
    char hdr[80];
    while (rb_copy_next(s_rb, after, end_ts, tmp, CLIP_FRAME_CAP, &flen, &fts)) {
        after = fts;
        int n = snprintf(hdr, sizeof(hdr), PART_HDR, (unsigned)flen);
        if (httpd_resp_send_chunk(r, hdr, n) != ESP_OK) break;
        if (httpd_resp_send_chunk(r, (const char *)tmp, flen) != ESP_OK) break;
    }
    free(tmp);
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
    cfg.max_open_sockets = 5;   // +listen +ctrl = 7 of the 16-socket LWIP pool
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return; }
    reg(srv, "/",          HTTP_GET,  h_index);
    reg(srv, "/app.js",    HTTP_GET,  h_appjs);
    reg(srv, "/style.css", HTTP_GET,  h_css);
    reg(srv, "/snapshot",  HTTP_GET,  h_snapshot);
    reg(srv, "/clip",      HTTP_GET,  h_clip);
    reg(srv, "/api/status",HTTP_GET,  h_status);
    reg(srv, "/api/config",HTTP_POST, h_config);
    reg(srv, "/api/wifi",  HTTP_GET,  h_wifi_get);
    reg(srv, "/api/wifi",  HTTP_POST, h_wifi_post);
    ESP_LOGI(TAG, "http server up");

    // esp_http_server services every handler in ONE task, and /stream never
    // returns while a client is watching. Serve it from a second instance on a
    // separate port so the long-lived stream can't block status polling,
    // snapshots, or config changes on the main server.
    httpd_config_t scfg = HTTPD_DEFAULT_CONFIG();
    scfg.server_port = STREAM_PORT;
    scfg.ctrl_port   = cfg.ctrl_port + 1;   // must differ from the main server
    scfg.lru_purge_enable = true;
    scfg.stack_size  = 8192;
    scfg.max_open_sockets = 3;   // +listen +ctrl = 5; 7+5 = 12 of 16, 4 spare
    httpd_handle_t stream_srv = NULL;
    if (httpd_start(&stream_srv, &scfg) == ESP_OK) {
        reg(stream_srv, "/stream", HTTP_GET, h_stream);
        ESP_LOGI(TAG, "stream server up (port %d)", STREAM_PORT);
    } else {
        ESP_LOGE(TAG, "stream httpd start failed");
    }
}

void wearcam_mdns_start(void)
{
    if (mdns_init() != ESP_OK) return;
    mdns_hostname_set("wearcam");
    mdns_instance_name_set("WearCam");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mdns: wearcam.local");
}
