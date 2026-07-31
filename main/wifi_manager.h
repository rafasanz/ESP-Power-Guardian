#pragma once

#include <stddef.h>
#include <stdint.h>

struct WifiScanResult {
    char ssid[33];
    int8_t rssi;
};

enum class WifiScanState {
    idle,
    running,
    ready,
    failed,
};

void wifi_manager_start();
bool wifi_manager_connected();
const char *wifi_manager_ip();
bool wifi_manager_ap_active();
WifiScanState wifi_manager_start_scan();
WifiScanState wifi_manager_scan_state();
size_t wifi_manager_scan_results(WifiScanResult *results, size_t capacity);

