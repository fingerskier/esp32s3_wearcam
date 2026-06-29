#pragma once
#include <stdbool.h>
#include <stddef.h>

void wifi_start(void);
bool wifi_set_credentials(const char *ssid, const char *pass);
bool wifi_is_connected(void);
bool wifi_is_provisioning(void);
void wifi_get_ip(char *out, size_t n);
int  wifi_get_rssi(void);
