#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>

#define MAX_STORED_LOGS         15  // 15 test slots in FRAM
#define MAX_PRELOADED_SITES     50  // Up to 50 customer sites loaded from /customers/sites.csv

// Status codes returned when saving a new test log
enum SaveLogResult {
    SAVE_ERROR,                 // FRAM hardware failure
    SAVE_OK,                    // Saved cleanly into free space
    SAVE_OVERWROTE_SYNCED,      // Storage full: auto-deleted oldest synced ('S') log
    SAVE_OVERWROTE_PENDING      // Storage full: forced overwrite of oldest unsynced ('P') log
};

// Fixed-size 44-byte structure stored sequentially in FRAM
struct TestLog {
    uint32_t logId;             // Sequential test ID (1, 2, 3...)
    uint32_t durationSec;       // How long the test ran in seconds
    float totalVolumeLiters;    // Measured water volume in Liters
    float peakPressureBar;      // Maximum water pressure recorded in Bar
    uint8_t testMode;           // 0 = Full Test, 1 = Pressure Only, 2 = Volume Only
    char syncStatus;            // 'P' = Pending sync, 'S' = Synced to phone
    char siteAddress[24];       // Site address / Customer ID (up to 23 chars + null terminator)
    uint8_t reserved[2];        // 2-byte padding for clean 44-byte alignment
};

// Initialize FRAM chip, microSD card, and load saved calibration values
bool initStorageManager();

// --- Calibration Persistence (FRAM) ---
void saveCalibrationToFRAM(float kFactor, float pressureOffset, float maxPressureBar);
void loadCalibrationFromFRAM(float& kFactor, float& pressureOffset, float& maxPressureBar);

// --- MicroSD Customer Site Pre-Load (/customers/sites.csv) ---
bool isSDCardReady();
uint16_t reloadSitesFromSD();
uint16_t getPreloadedSiteCount();
String getPreloadedSite(uint16_t index);

// --- Test Log Management (FRAM + SD Backup) ---
SaveLogResult saveNewTestLog(const String& siteAddress, uint32_t durationSec, float volumeLiters, float peakPressureBar, uint8_t testMode);
bool readTestLogByIndex(uint16_t index, TestLog& outLog);
bool deleteTestLogByIndex(uint16_t index);
void markLogAsSynced(uint16_t index);
void clearAllStoredLogs();

// --- Storage Status & Capacity Metrics ---
uint16_t getStoredLogCount();
uint16_t getFreeLogSlots();
uint8_t getStorageUsagePercent();
uint16_t getPendingSyncCount();

// Helper to convert a log into a comma-separated string
String formatLogAsCSV(const TestLog& log);

#endif // STORAGE_MANAGER_H
