#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>

#define MAX_SCANNED_NETWORKS 15

// Mutually exclusive gateway modes
enum GatewayMode {
    MODE_WIFI,
    MODE_BLUETOOTH
};

// Initialize network subsystem and load saved Wi-Fi credentials
void initNetworkManager();

// Periodic background handler (call in main loop)
void updateNetwork();

// Strictly tears down the active radio before initializing the target radio
void switchGateway(GatewayMode targetMode);

// --- Wi-Fi Scanner Helpers (For 4-Button OLED UI) ---
int scanAvailableWiFiNetworks();          // Scans nearby hotspots and returns count found
String getScannedSSID(int index);         // Gets SSID name at index
int32_t getScannedRSSI(int index);        // Gets signal strength (dBm) at index
void clearWiFiScanResults();              // Frees scan memory when leaving the UI menu

// --- Credential Management & Connection ---
void saveAndConnectWiFi(const String& ssid, const String& password);
String getSavedWiFiSSID();

// --- Status Getters for OLED Display ---
GatewayMode getActiveGatewayMode();
bool isNetworkConnected();
int16_t getSignalStrength();
String getNetworkStatusText();

// Streams all pending ('P') logs from FRAM over the active link and marks them synced ('S')
uint16_t syncPendingLogs();

#endif // NETWORK_MANAGER_H
