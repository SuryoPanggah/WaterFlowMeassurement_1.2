#include "network_manager.h"
#include "storage_manager.h"
#include "config.h"
#include <WiFi.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include "esp_bt.h"
#include "esp_bt_main.h"

static BluetoothSerial SerialBT;
static Preferences netPrefs;

static GatewayMode currentMode = MODE_WIFI;
static String savedSSID = "";
static String savedPass = "";
static int scannedCount = 0;

// Complete hardware teardown of both radios to guarantee zero overlap
static void stopAllRadios() {
    // 1. Shut down Wi-Fi completely
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // 2. Shut down Bluetooth Classic / SPP completely
    if (SerialBT.hasClient() || btStarted()) {
        SerialBT.end();
    }
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
    }

    delay(150); // Allow internal RF power rails to stabilize
}

static void startWiFiClient() {
    stopAllRadios();
    Serial.println("[Network] Starting Wi-Fi Client mode...");
    WiFi.mode(WIFI_STA);

    if (savedSSID.length() > 0) {
        Serial.print("[Network] Connecting to saved SSID: ");
        Serial.println(savedSSID);
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    } else {
        Serial.println("[Network] No saved Wi-Fi credentials. Waiting for user scan.");
    }
}

static void startBluetoothClient() {
    stopAllRadios();
    Serial.println("[Network] Starting Bluetooth Classic SPP mode...");
    if (!SerialBT.begin(DEVICE_BT_NAME)) {
        Serial.println("[Network] ERROR: Bluetooth initialization failed!");
    } else {
        Serial.print("[Network] Bluetooth ready as: ");
        Serial.println(DEVICE_BT_NAME);
    }
}

void initNetworkManager() {
    // Load stored Wi-Fi credentials from ESP32 non-volatile storage (NVS)
    netPrefs.begin("water_net", true); // Read-only mode
    savedSSID = netPrefs.getString("ssid", "");
    savedPass = netPrefs.getString("pass", "");
    netPrefs.end();

    // Default boot gateway: Wi-Fi Client
    switchGateway(MODE_WIFI);
}

void switchGateway(GatewayMode targetMode) {
    currentMode = targetMode;
    if (currentMode == MODE_WIFI) {
        startWiFiClient();
    } else {
        startBluetoothClient();
    }
}

// =========================================================================
// WI-FI SCANNER & ON-DEVICE CREDENTIAL STORAGE
// =========================================================================

int scanAvailableWiFiNetworks() {
    // Ensure Bluetooth is off and Wi-Fi station mode is active before scanning
    if (currentMode != MODE_WIFI) {
        switchGateway(MODE_WIFI);
    }
    WiFi.disconnect();
    delay(100);

    Serial.println("[Network] Scanning nearby Wi-Fi networks...");
    int n = WiFi.scanNetworks();
    scannedCount = (n > MAX_SCANNED_NETWORKS) ? MAX_SCANNED_NETWORKS : (n < 0 ? 0 : n);

    Serial.print("[Network] Networks found: ");
    Serial.println(scannedCount);
    return scannedCount;
}

String getScannedSSID(int index) {
    if (index < 0 || index >= scannedCount) return "";
    return WiFi.SSID(index);
}

int32_t getScannedRSSI(int index) {
    if (index < 0 || index >= scannedCount) return -100;
    return WiFi.RSSI(index);
}

void clearWiFiScanResults() {
    WiFi.scanDelete();
    scannedCount = 0;
}

void saveAndConnectWiFi(const String& ssid, const String& password) {
    savedSSID = ssid;
    savedPass = password;

    // Persist permanently to ESP32 NVS Flash
    netPrefs.begin("water_net", false); // Read-write mode
    netPrefs.putString("ssid", savedSSID);
    netPrefs.putString("pass", savedPass);
    netPrefs.end();

    Serial.print("[Network] Saved credentials for: ");
    Serial.println(savedSSID);

    // Connect immediately using the new credentials
    if (currentMode != MODE_WIFI) {
        switchGateway(MODE_WIFI);
    } else {
        WiFi.disconnect();
        delay(100);
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    }
}

String getSavedWiFiSSID() {
    return savedSSID;
}

// =========================================================================
// STATUS GETTERS & DATA SYNC
// =========================================================================

GatewayMode getActiveGatewayMode() {
    return currentMode;
}

bool isNetworkConnected() {
    if (currentMode == MODE_WIFI) {
        return (WiFi.status() == WL_CONNECTED);
    } else {
        return SerialBT.hasClient();
    }
}

int16_t getSignalStrength() {
    if (currentMode == MODE_WIFI && WiFi.status() == WL_CONNECTED) {
        return (int16_t)WiFi.RSSI();
    }
    return 0;
}

String getNetworkStatusText() {
    if (currentMode == MODE_WIFI) {
        if (savedSSID.length() == 0) {
            return "WiFi: Not Set";
        }
        if (WiFi.status() == WL_CONNECTED) {
            return "WiFi OK (" + String(WiFi.RSSI()) + "dBm)";
        }
        return "WiFi Connecting...";
    } else {
        if (SerialBT.hasClient()) {
            return "BT Paired & Ready";
        }
        return "BT Waiting Pair...";
    }
}

void updateNetwork() {
    // Allow remote sync trigger if paired over Bluetooth Serial
    if (currentMode == MODE_BLUETOOTH && SerialBT.available()) {
        String cmd = SerialBT.readStringUntil('\n');
        cmd.trim();

        if (cmd == "SYNC") {
            uint16_t synced = syncPendingLogs();
            SerialBT.println("ACK_SYNC:" + String(synced));
        }
    }
}

uint16_t syncPendingLogs() {
    if (!isNetworkConnected()) {
        Serial.println("[Network] Sync aborted: No active connection.");
        return 0;
    }

    uint16_t totalLogs = getStoredLogCount();
    uint16_t syncedCount = 0;
    TestLog currentLog;

    String header = "ID,MODE,DURATION,VOLUME,PEAK_PRESSURE,STATUS";
    if (currentMode == MODE_BLUETOOTH) {
        SerialBT.println(header);
    } else {
        Serial.println(header); // Hook for HTTP POST / TCP server endpoint
    }

    for (uint16_t i = 0; i < totalLogs; i++) {
        if (readTestLogByIndex(i, currentLog) && currentLog.syncStatus == 'P') {
            String csvLine = formatLogAsCSV(currentLog);

            if (currentMode == MODE_BLUETOOTH) {
                SerialBT.println(csvLine);
            } else {
                Serial.println("[WiFi TX] " + csvLine);
            }

            markLogAsSynced(i);
            syncedCount++;
            delay(20);
        }
    }

    Serial.print("[Network] Synced logs count: ");
    Serial.println(syncedCount);
    return syncedCount;
}
