#pragma once
#include <stdbool.h>
#include <stddef.h>

void wifi_start(void);
bool wifi_set_credentials(const char *ssid, const char *pass);
bool wifi_is_connected(void);
bool wifi_is_provisioning(void);
void wifi_get_ip(char *out, size_t n);
int  wifi_get_rssi(void);

// Registers a callback fired (from the Wi-Fi event task) each time the STA
// acquires an IP. Lets BLE push the fresh device IP to the dashboard.
typedef void (*wifi_ip_cb_t)(void);
void wifi_set_ip_cb(wifi_ip_cb_t cb);
