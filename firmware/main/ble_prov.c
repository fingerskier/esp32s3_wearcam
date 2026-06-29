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

static int gap_event(struct ble_gap_event *ev, void *arg);

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
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
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
}
