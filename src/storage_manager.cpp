#include "storage_manager.h"
#include "config.h"
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <FRAM.h>

static FRAM fram;
static bool framReady = false;
static bool sdReady = false;

// In-memory cache of pre-loaded customer sites from /customers/sites.csv
static String preloadedSites[MAX_PRELOADED_SITES];
static uint16_t preloadedSiteCount = 0;

// =========================================================================
// FRAM MEMORY MAP (Total Capacity: 32,768 Bytes on MB85RC256V)
// =========================================================================
// 0x0000 - 0x0003 : Magic Header (0x57415452 = "WATR")
// 0x0004 - 0x0007 : Float - Flow K-Factor
// 0x0008 - 0x000B : Float - Pressure Zero Offset
// 0x000C - 0x000D : uint16_t - Total Stored Log Count (0 to 15)
// 0x0010 - 0x0013 : uint32_t - Lifetime Sequential Log ID Counter
// 0x0014 - 0x0017 : Float - Max Pressure Transmitter Rating (Bar)
// 0x0100 onwards  : Array of TestLog structs (44 bytes * 15 = 660 bytes)
// =========================================================================
#define ADDR_MAGIC_HEADER 0x0000
#define ADDR_FLOW_K_FACTOR 0x0004
#define ADDR_PRESSURE_OFFSET 0x0008
#define ADDR_LOG_COUNT 0x000C
#define ADDR_NEXT_LOG_ID 0x0010
#define ADDR_MAX_PRESSURE_BAR 0x0014
#define ADDR_LOGS_START 0x0100

#define FRAM_MAGIC_VALUE 0x57415452UL

static void appendLogToSDBackup(const TestLog &log)
{
    if (!sdReady)
        return;

    if (!SD.exists("/logs"))
    {
        SD.mkdir("/logs");
    }

    bool writeHeader = !SD.exists("/logs/audit_backup.csv");
    File f = SD.open("/logs/audit_backup.csv", FILE_APPEND);
    if (!f)
        return;

    if (writeHeader)
    {
        f.println("ID,SITE_ADDRESS,MODE,DURATION,VOLUME,PEAK_PRESSURE,STATUS");
    }
    f.println(formatLogAsCSV(log));
    f.close();
}

bool initStorageManager()
{
    // 1. Mount MB85RC256V FRAM over I2C
    if (fram.begin(FRAM_I2C_ADDRESS) != FRAM_OK)
    {
        Serial.println("[Storage] ERROR: MB85RC256V FRAM not found at 0x50!");
        framReady = false;
    }
    else
    {
        framReady = true;
        uint32_t magic = fram.read32(ADDR_MAGIC_HEADER);
        if (magic != FRAM_MAGIC_VALUE)
        {
            Serial.println("[Storage] Blank FRAM detected. Formatting initial memory map...");
            fram.write32(ADDR_MAGIC_HEADER, FRAM_MAGIC_VALUE);
            saveCalibrationToFRAM(DEFAULT_FLOW_K_FACTOR, DEFAULT_PRESSURE_OFFSET, DEFAULT_MAX_PRESSURE_BAR);
            fram.write16(ADDR_LOG_COUNT, 0);
            fram.write32(ADDR_NEXT_LOG_ID, 1);
        }
        else
        {
            Serial.println("[Storage] FRAM mounted successfully.");
        }
    }

    // 2. Mount microSD card on shared SPI bus (deselect TFT CS first)
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    SPI.begin(TFT_SCK_PIN, TFT_MISO_PIN, TFT_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, SPI))
    {
        Serial.println("[Storage] No microSD card detected (or mount failed).");
        sdReady = false;
    }
    else
    {
        sdReady = true;
        Serial.println("[Storage] microSD card mounted successfully.");
        reloadSitesFromSD();
    }

    return framReady;
}

// =========================================================================
// CALIBRATION PERSISTENCE (FRAM)
// =========================================================================
void saveCalibrationToFRAM(float kFactor, float pressureOffset, float maxPressureBar)
{
    if (!framReady)
        return;
    fram.writeFloat(ADDR_FLOW_K_FACTOR, kFactor);
    fram.writeFloat(ADDR_PRESSURE_OFFSET, pressureOffset);
    fram.writeFloat(ADDR_MAX_PRESSURE_BAR, maxPressureBar);
    Serial.println("[Storage] Calibration saved to FRAM.");
}

void loadCalibrationFromFRAM(float &kFactor, float &pressureOffset, float &maxPressureBar)
{
    if (!framReady)
    {
        kFactor = DEFAULT_FLOW_K_FACTOR;
        pressureOffset = DEFAULT_PRESSURE_OFFSET;
        maxPressureBar = DEFAULT_MAX_PRESSURE_BAR;
        return;
    }
    kFactor = fram.readFloat(ADDR_FLOW_K_FACTOR);
    pressureOffset = fram.readFloat(ADDR_PRESSURE_OFFSET);
    maxPressureBar = fram.readFloat(ADDR_MAX_PRESSURE_BAR);

    if (isnan(kFactor) || kFactor <= 0.1f)
        kFactor = DEFAULT_FLOW_K_FACTOR;
    if (isnan(pressureOffset))
        pressureOffset = DEFAULT_PRESSURE_OFFSET;
    if (isnan(maxPressureBar) || maxPressureBar <= 0.5f || maxPressureBar > 100.0f)
    {
        maxPressureBar = DEFAULT_MAX_PRESSURE_BAR;
    }
}

// =========================================================================
// MICROSD CUSTOMER SITE PRE-LOAD (/customers/sites.csv)
// =========================================================================
bool isSDCardReady()
{
    return sdReady;
}

uint16_t reloadSitesFromSD()
{
    preloadedSiteCount = 0;
    if (!sdReady)
        return 0;

    if (!SD.exists(SD_SITES_CSV_PATH))
    {
        Serial.println("[Storage] /customers/sites.csv not found on SD.");
        return 0;
    }

    File file = SD.open(SD_SITES_CSV_PATH, FILE_READ);
    if (!file)
    {
        Serial.println("[Storage] Failed to open /customers/sites.csv.");
        return 0;
    }

    bool firstLine = true;
    while (file.available() && preloadedSiteCount < MAX_PRELOADED_SITES)
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            continue;

        // Support either 1-column ("JL SUDIRMAN 12") or 2-column ("CUST01,JL SUDIRMAN 12")
        int commaIdx = line.indexOf(',');
        String siteStr = (commaIdx >= 0) ? line.substring(commaIdx + 1) : line;
        siteStr.replace("\"", "");
        siteStr.trim();

        // Skip common CSV header rows on the first line
        if (firstLine)
        {
            firstLine = false;
            String upper = siteStr;
            upper.toUpperCase();
            if (upper == "SITE" || upper == "SITE_ADDRESS" || upper == "ADDRESS" || upper == "ALAMAT")
            {
                continue;
            }
        }

        if (siteStr.length() > 0)
        {
            if (siteStr.length() > 23)
                siteStr = siteStr.substring(0, 23);
            preloadedSites[preloadedSiteCount++] = siteStr;
        }
    }

    file.close();
    Serial.print("[Storage] Loaded sites from SD: ");
    Serial.println(preloadedSiteCount);
    return preloadedSiteCount;
}

uint16_t getPreloadedSiteCount()
{
    return preloadedSiteCount;
}

String getPreloadedSite(uint16_t index)
{
    if (index >= preloadedSiteCount)
        return "";
    return preloadedSites[index];
}

// =========================================================================
// TEST LOG MANAGEMENT (FRAM + SD BACKUP)
// =========================================================================
bool deleteTestLogByIndex(uint16_t index)
{
    if (!framReady)
        return false;
    uint16_t count = getStoredLogCount();
    if (index >= count)
        return false;

    TestLog tempLog;
    for (uint16_t i = index; i < count - 1; i++)
    {
        uint16_t nextAddr = ADDR_LOGS_START + ((i + 1) * sizeof(TestLog));
        uint16_t currAddr = ADDR_LOGS_START + (i * sizeof(TestLog));
        fram.read(nextAddr, (uint8_t *)&tempLog, sizeof(TestLog));
        fram.write(currAddr, (uint8_t *)&tempLog, sizeof(TestLog));
    }

    fram.write16(ADDR_LOG_COUNT, count - 1);
    return true;
}

SaveLogResult saveNewTestLog(const String &siteAddress, uint32_t durationSec, float volumeLiters, float peakPressureBar, uint8_t testMode)
{
    if (!framReady)
        return SAVE_ERROR;

    SaveLogResult result = SAVE_OK;
    uint16_t count = getStoredLogCount();

    if (count >= MAX_STORED_LOGS)
    {
        int16_t oldestSyncedIndex = -1;
        TestLog scanLog;

        for (uint16_t i = 0; i < count; i++)
        {
            if (readTestLogByIndex(i, scanLog) && scanLog.syncStatus == 'S')
            {
                oldestSyncedIndex = i;
                break;
            }
        }

        if (oldestSyncedIndex >= 0)
        {
            deleteTestLogByIndex((uint16_t)oldestSyncedIndex);
            result = SAVE_OVERWROTE_SYNCED;
        }
        else
        {
            deleteTestLogByIndex(0);
            result = SAVE_OVERWROTE_PENDING;
        }
        count = getStoredLogCount();
    }

    uint32_t nextId = fram.read32(ADDR_NEXT_LOG_ID);

    TestLog newLog;
    newLog.logId = nextId;
    newLog.durationSec = durationSec;
    newLog.totalVolumeLiters = volumeLiters;
    newLog.peakPressureBar = peakPressureBar;
    newLog.testMode = testMode;
    newLog.syncStatus = 'P';
    memset(newLog.siteAddress, 0, sizeof(newLog.siteAddress));
    strncpy(newLog.siteAddress, siteAddress.c_str(), sizeof(newLog.siteAddress) - 1);
    newLog.reserved[0] = 0;
    newLog.reserved[1] = 0;

    uint16_t writeAddr = ADDR_LOGS_START + (count * sizeof(TestLog));
    fram.write(writeAddr, (uint8_t *)&newLog, sizeof(TestLog));

    fram.write16(ADDR_LOG_COUNT, count + 1);
    fram.write32(ADDR_NEXT_LOG_ID, nextId + 1);

    // Also append a permanent backup copy to the microSD card if present
    appendLogToSDBackup(newLog);

    Serial.print("[Storage] Saved Test Log #");
    Serial.print(nextId);
    Serial.print(" at Site: ");
    Serial.println(newLog.siteAddress);
    return result;
}

bool readTestLogByIndex(uint16_t index, TestLog &outLog)
{
    if (!framReady)
        return false;
    uint16_t count = getStoredLogCount();
    if (index >= count)
        return false;

    uint16_t readAddr = ADDR_LOGS_START + (index * sizeof(TestLog));
    fram.read(readAddr, (uint8_t *)&outLog, sizeof(TestLog));
    return true;
}

void markLogAsSynced(uint16_t index)
{
    TestLog log;
    if (readTestLogByIndex(index, log))
    {
        log.syncStatus = 'S';
        uint16_t writeAddr = ADDR_LOGS_START + (index * sizeof(TestLog));
        fram.write(writeAddr, (uint8_t *)&log, sizeof(TestLog));
    }
}

void clearAllStoredLogs()
{
    if (!framReady)
        return;
    fram.write16(ADDR_LOG_COUNT, 0);
    Serial.println("[Storage] All stored logs cleared.");
}
uint16_t getStoredLogCount()
{
    if (!framReady)
        return 0;
    uint16_t count = fram.read16(ADDR_LOG_COUNT);
    return (count > MAX_STORED_LOGS) ? MAX_STORED_LOGS : count;
}
uint16_t getFreeLogSlots()
{
    uint16_t used = getStoredLogCount();
    return (used >= MAX_STORED_LOGS) ? 0 : (MAX_STORED_LOGS - used);
}
uint8_t getStorageUsagePercent()
{
    uint16_t used = getStoredLogCount();
    return (uint8_t)((used * 100UL) / MAX_STORED_LOGS);
}
uint16_t getPendingSyncCount()
{
    if (!framReady)
        return 0;
    uint16_t count = getStoredLogCount();
    uint16_t pending = 0;
    TestLog tempLog;
    for (uint16_t i = 0; i < count; i++)
    {
        if (readTestLogByIndex(i, tempLog) && tempLog.syncStatus == 'P')
        {
            pending++;
        }
    }
    return pending;
}
String formatLogAsCSV(const TestLog &log)
{
    String modeStr = (log.testMode == 0) ? "FULL" : (log.testMode == 1 ? "PRES" : "VOL");
    String csv = String(log.logId) + ",";
    csv += String(log.siteAddress) + ",";
    csv += modeStr + ",";
    csv += String(log.durationSec) + "s,";
    csv += String(log.totalVolumeLiters, 2) + "L,";
    csv += String(log.peakPressureBar, 2) + "Bar,";
    csv += String(log.syncStatus);
    return csv;
}