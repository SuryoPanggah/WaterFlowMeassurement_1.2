#include "menu_manager.h"
#include "config.h"
#include "power_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "network_manager.h"
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// =========================================================================
// MENU STATE MACHINE DEFINITIONS
// =========================================================================
enum ScreenState
{
    SCR_MAIN_MENU,
    SCR_SITE_SETUP,
    SCR_SELECT_MODE,
    SCR_SELECT_DURATION,
    SCR_TEST_RUNNING,
    SCR_NETWORK_MENU,
    SCR_GATEWAY_SELECT,
    SCR_WIFI_SCAN_LIST,
    SCR_VIRTUAL_KEYBOARD,
    SCR_CONN_TEST,
    SCR_STORAGE_MENU,
    SCR_VIEW_LOGS,
    SCR_CLEAR_CONFIRM,
    SCR_TOOLS_MENU,
    SCR_CALIBRATE_FLOW,
    SCR_OLED_TEST,
    SCR_DEVICE_STATUS
};

enum KeyboardPurpose
{
    KB_FOR_WIFI_PASS,
    KB_FOR_SITE_ADDR
};

static ScreenState currentScreen = SCR_MAIN_MENU;
static int cursorIdx = 0;

// Temporary popup toast banner state
static String toastMsg = "";
static unsigned long toastExpireTime = 0;

// Active Site & Test Tracking Variables
static String activeSiteAddress = "";    // Persists across multiple tests at same site
static uint8_t activeTestMode = 0;       // 0 = Full, 1 = Pressure Only, 2 = Volume Only
static uint32_t targetDurationSec = 900; // Default: 15 minutes (900s), 0 = Manual
static unsigned long testStartTime = 0;
static float testPeakPressure = 0.0f;

static const uint16_t durationPresetsMin[] = {15, 10, 5, 1, 0};
static const int TOTAL_DURATION_PRESETS = 5;

// Log Viewer Scroll Index
static int logViewIndex = 0;

// Wi-Fi Scanner & Reusable Virtual Keyboard State
static int scannedNetCount = 0;
static String selectedWiFiSSID = "";
static KeyboardPurpose kbPurpose = KB_FOR_SITE_ADDR;
static String kbInputBuffer = "";
static int kbMaxLen = 23;
static int keyCharIndex = 0;

// Character set (includes space ' ' and common address/password symbols; index 78 = [OK])
static const char *kbChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_./abcdefghijklmnopqrstuvwxyz!@#$%^&*+=?";
static const int KB_CHAR_LEN = 78;

static void showToast(const String &msg, uint16_t durationMs = 1500)
{
    toastMsg = msg;
    toastExpireTime = millis() + durationMs;
    beepBuzzer(60);
}

static void changeScreen(ScreenState nextScreen, int resetCursor = 0)
{
    currentScreen = nextScreen;
    cursorIdx = resetCursor;
}

static void openKeyboard(KeyboardPurpose purpose, const String &initialText, int maxLen)
{
    kbPurpose = purpose;
    kbInputBuffer = initialText;
    kbMaxLen = maxLen;
    keyCharIndex = 0;
    changeScreen(SCR_VIRTUAL_KEYBOARD);
}

static void finishAndSaveTest(uint32_t elapsedSec)
{
    setSolenoidState(false);
    float totalVol = getTotalVolumeLiters();
    String siteToSave = (activeSiteAddress.length() > 0) ? activeSiteAddress : "UNASSIGNED";

    SaveLogResult res = saveNewTestLog(siteToSave, elapsedSec, totalVol, testPeakPressure, activeTestMode);
    if (res == SAVE_OK)
        showToast("Test Saved to FRAM!");
    else if (res == SAVE_OVERWROTE_SYNCED)
        showToast("Saved (Old Synced Replaced)");
    else if (res == SAVE_OVERWROTE_PENDING)
        showToast("Saved (Oldest Overwritten)");
    else
        showToast("FRAM Save Error!");

    beepBuzzer(150);
    changeScreen(SCR_MAIN_MENU, 0);
}

void initMenuManager()
{
    currentScreen = SCR_MAIN_MENU;
    cursorIdx = 0;
}

// =========================================================================
// 4-BUTTON INPUT HANDLER
// =========================================================================
void handleMenuInput(bool upPressed, bool downPressed, bool selectPressed, bool backPressed)
{
    if (!upPressed && !downPressed && !selectPressed && !backPressed)
        return;
    toastExpireTime = 0;

    // --- 1. MAIN MENU ---
    if (currentScreen == SCR_MAIN_MENU)
    {
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? 4 : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= 4) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                changeScreen(SCR_SITE_SETUP, activeSiteAddress.length() > 0 ? 0 : 1);
            }
            else if (cursorIdx == 1)
                changeScreen(SCR_NETWORK_MENU);
            else if (cursorIdx == 2)
                changeScreen(SCR_STORAGE_MENU);
            else if (cursorIdx == 3)
                changeScreen(SCR_TOOLS_MENU);
            else if (cursorIdx == 4)
                changeScreen(SCR_DEVICE_STATUS);
        }
        return;
    }

    // --- 2. SITE SETUP -> SELECT MODE -> SET DURATION ---
    if (currentScreen == SCR_SITE_SETUP)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 0);
            return;
        }
        if (upPressed || downPressed)
            cursorIdx = (cursorIdx == 0) ? 1 : 0;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                if (activeSiteAddress.length() == 0)
                {
                    showToast("Set Site Address First!");
                }
                else
                {
                    changeScreen(SCR_SELECT_MODE, 0);
                }
            }
            else
            {
                openKeyboard(KB_FOR_SITE_ADDR, activeSiteAddress, 23);
            }
        }
        return;
    }

    if (currentScreen == SCR_SELECT_MODE)
    {
        if (backPressed)
        {
            changeScreen(SCR_SITE_SETUP, 0);
            return;
        }
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? 2 : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= 2) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            activeTestMode = (uint8_t)cursorIdx;
            changeScreen(SCR_SELECT_DURATION, 0);
        }
        return;
    }

    if (currentScreen == SCR_SELECT_DURATION)
    {
        if (backPressed)
        {
            changeScreen(SCR_SELECT_MODE, activeTestMode);
            return;
        }
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? (TOTAL_DURATION_PRESETS - 1) : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= TOTAL_DURATION_PRESETS - 1) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            uint16_t chosenMins = durationPresetsMin[cursorIdx];
            targetDurationSec = (uint32_t)chosenMins * 60UL;

            resetFlowCounters();
            testPeakPressure = 0.0f;
            testStartTime = millis();

            setSolenoidState(activeTestMode != 1);
            beepBuzzer(100);
            changeScreen(SCR_TEST_RUNNING);
        }
        return;
    }

    // --- 3. TEST RUNNING ---
    if (currentScreen == SCR_TEST_RUNNING)
    {
        if (selectPressed)
        {
            uint32_t elapsedSec = (millis() - testStartTime) / 1000UL;
            finishAndSaveTest(elapsedSec);
        }
        else if (backPressed)
        {
            setSolenoidState(false);
            showToast("Test Cancelled");
            changeScreen(SCR_MAIN_MENU, 0);
        }
        return;
    }

    // --- 4. NETWORK / SYNC MENU & WIFI SETUP ---
    if (currentScreen == SCR_NETWORK_MENU)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 1);
            return;
        }
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? 3 : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= 3) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                uint16_t synced = syncPendingLogs();
                showToast("Synced " + String(synced) + " Log(s)");
            }
            else if (cursorIdx == 1)
            {
                changeScreen(SCR_GATEWAY_SELECT, getActiveGatewayMode() == MODE_WIFI ? 0 : 1);
            }
            else if (cursorIdx == 2)
            {
                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_profont11_mf);
                u8g2.drawStr(15, 34, "Scanning Wi-Fi...");
                u8g2.sendBuffer();

                scannedNetCount = scanAvailableWiFiNetworks();
                if (scannedNetCount == 0)
                    showToast("No Wi-Fi Found");
                else
                    changeScreen(SCR_WIFI_SCAN_LIST, 0);
            }
            else if (cursorIdx == 3)
            {
                changeScreen(SCR_CONN_TEST);
            }
        }
        return;
    }

    if (currentScreen == SCR_GATEWAY_SELECT)
    {
        if (backPressed)
        {
            changeScreen(SCR_NETWORK_MENU, 1);
            return;
        }
        if (upPressed || downPressed)
            cursorIdx = (cursorIdx == 0) ? 1 : 0;
        if (selectPressed)
        {
            switchGateway(cursorIdx == 0 ? MODE_WIFI : MODE_BLUETOOTH);
            showToast(cursorIdx == 0 ? "Switched to Wi-Fi" : "Switched to BT");
            changeScreen(SCR_NETWORK_MENU, 1);
        }
        return;
    }

    if (currentScreen == SCR_WIFI_SCAN_LIST)
    {
        if (backPressed)
        {
            clearWiFiScanResults();
            changeScreen(SCR_NETWORK_MENU, 2);
            return;
        }
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? (scannedNetCount - 1) : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= scannedNetCount - 1) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            selectedWiFiSSID = getScannedSSID(cursorIdx);
            clearWiFiScanResults();
            openKeyboard(KB_FOR_WIFI_PASS, "", 32);
        }
        return;
    }

    // --- REUSABLE VIRTUAL KEYBOARD (SITE ADDRESS & WIFI PASSWORD) ---
    if (currentScreen == SCR_VIRTUAL_KEYBOARD)
    {
        if (upPressed)
            keyCharIndex = (keyCharIndex <= 0) ? KB_CHAR_LEN : keyCharIndex - 1;
        if (downPressed)
            keyCharIndex = (keyCharIndex >= KB_CHAR_LEN) ? 0 : keyCharIndex + 1;
        if (selectPressed)
        {
            if (keyCharIndex == KB_CHAR_LEN)
            {
                if (kbPurpose == KB_FOR_WIFI_PASS)
                {
                    saveAndConnectWiFi(selectedWiFiSSID, kbInputBuffer);
                    showToast("Wi-Fi Saved & Connecting");
                    changeScreen(SCR_NETWORK_MENU, 2);
                }
                else
                {
                    if (kbInputBuffer.length() == 0)
                    {
                        showToast("Address Cannot Be Empty");
                    }
                    else
                    {
                        activeSiteAddress = kbInputBuffer;
                        showToast("Site Address Set!");
                        changeScreen(SCR_SELECT_MODE, 0);
                    }
                }
            }
            else
            {
                if ((int)kbInputBuffer.length() < kbMaxLen)
                {
                    kbInputBuffer += kbChars[keyCharIndex];
                }
            }
        }
        if (backPressed)
        {
            if (kbInputBuffer.length() > 0)
            {
                kbInputBuffer.remove(kbInputBuffer.length() - 1);
            }
            else
            {
                if (kbPurpose == KB_FOR_WIFI_PASS)
                    changeScreen(SCR_NETWORK_MENU, 2);
                else
                    changeScreen(SCR_SITE_SETUP, 1);
            }
        }
        return;
    }
    if (currentScreen == SCR_CONN_TEST)
    {
        if (backPressed || selectPressed)
            changeScreen(SCR_NETWORK_MENU, 3);
        return;
    }
    // --- 5. STORED DATA MENU ---
    if (currentScreen == SCR_STORAGE_MENU)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 2);
            return;
        }
        if (upPressed || downPressed)
            cursorIdx = (cursorIdx == 0) ? 1 : 0;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                uint16_t total = getStoredLogCount();
                if (total == 0)
                    showToast("No Logs Stored");
                else
                {
                    logViewIndex = total - 1;
                    changeScreen(SCR_VIEW_LOGS);
                }
            }
            else
            {
                changeScreen(SCR_CLEAR_CONFIRM, 1);
            }
        }
        return;
    }
    if (currentScreen == SCR_VIEW_LOGS)
    {
        uint16_t total = getStoredLogCount();
        if (backPressed || total == 0)
        {
            changeScreen(SCR_STORAGE_MENU, 0);
            return;
        }
        if (upPressed)
            logViewIndex = (logViewIndex + 1 >= total) ? 0 : logViewIndex + 1;
        if (downPressed)
            logViewIndex = (logViewIndex <= 0) ? total - 1 : logViewIndex - 1;
        if (selectPressed)
        {
            deleteTestLogByIndex((uint16_t)logViewIndex);
            showToast("Log Deleted");
            uint16_t remaining = getStoredLogCount();
            if (remaining == 0)
                changeScreen(SCR_STORAGE_MENU, 0);
            else if (logViewIndex >= remaining)
                logViewIndex = remaining - 1;
        }
        return;
    }
    if (currentScreen == SCR_CLEAR_CONFIRM)
    {
        if (backPressed)
        {
            changeScreen(SCR_STORAGE_MENU, 1);
            return;
        }
        if (upPressed || downPressed)
            cursorIdx = (cursorIdx == 0) ? 1 : 0;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                clearAllStoredLogs();
                showToast("All Memory Cleared");
            }
            changeScreen(SCR_STORAGE_MENU, 1);
        }
        return;
    }
    // --- 6. HARDWARE TOOLS MENU ---
    if (currentScreen == SCR_TOOLS_MENU)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 3);
            return;
        }
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? 3 : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= 3) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                bool nextState = !getSolenoidState();
                setSolenoidState(nextState);
                showToast(nextState ? "Valve OPENED" : "Valve CLOSED");
            }
            else if (cursorIdx == 1)
            {
                zeroPressureSensor();
                saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset());
                showToast("Pressure Zeroed!");
            }
            else if (cursorIdx == 2)
            {
                beepBuzzer(150);
                changeScreen(SCR_OLED_TEST);
            }
            else if (cursorIdx == 3)
            {
                changeScreen(SCR_CALIBRATE_FLOW);
            }
        }
        return;
    }
    if (currentScreen == SCR_CALIBRATE_FLOW)
    {
        float k = getFlowKFactor();
        if (upPressed)
            setFlowKFactor(k + 0.1f);
        if (downPressed)
            setFlowKFactor(k - 0.1f);
        if (selectPressed)
        {
            saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset());
            showToast("K-Factor Saved!");
            changeScreen(SCR_TOOLS_MENU, 3);
        }
        if (backPressed)
            changeScreen(SCR_TOOLS_MENU, 3);
        return;
    }
    if (currentScreen == SCR_OLED_TEST)
    {
        if (backPressed || selectPressed)
            changeScreen(SCR_TOOLS_MENU, 2);
        return;
    }
    // --- 7. DEVICE STATUS & DEEP SLEEP SHUTDOWN ---
    if (currentScreen == SCR_DEVICE_STATUS)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 4);
            return;
        }
        if (selectPressed)
            enterDeepSleep();
        return;
    }
}
// =========================================================================
// OLED SCREEN RENDERING ENGINE
// =========================================================================
void renderMenu()
{
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont11_mf);
    switch (currentScreen)
    {
    case SCR_MAIN_MENU:
    {
        const char *items[] = {
            "1. Start Test",
            "2. Network / Sync",
            "3. Stored Data",
            "4. Hardware Tools",
            "5. Device Status"};
        u8g2.drawStr(0, 10, "[ MAIN MENU ]");
        for (int i = 0; i < 5; i++)
        {
            int y = 22 + (i * 10);
            if (i == cursorIdx)
                u8g2.drawStr(0, y, ">");
            u8g2.drawStr(10, y, items[i]);
        }
        break;
    }
    case SCR_SITE_SETUP:
    {
        u8g2.drawStr(0, 10, "[ SITE LOCATION ]");
        u8g2.setCursor(0, 23);
        u8g2.print("Site: ");
        if (activeSiteAddress.length() == 0)
        {
            u8g2.print("(Not Set)");
        }
        else
        {
            String shortAddr = (activeSiteAddress.length() > 14)
                                   ? activeSiteAddress.substring(0, 14)
                                   : activeSiteAddress;
            u8g2.print(shortAddr);
        }
        u8g2.drawStr(0, 38, cursorIdx == 0 ? ">" : " ");
        u8g2.drawStr(10, 38, "Use Current Site");
        u8g2.drawStr(0, 50, cursorIdx == 1 ? ">" : " ");
        u8g2.drawStr(10, 50, "Edit / New Address");
        break;
    }
    case SCR_SELECT_MODE:
    {
        const char *modes[] = {"Full Test", "Pressure Only", "Volume Only"};
        u8g2.drawStr(0, 10, "[ SELECT TEST MODE ]");
        for (int i = 0; i < 3; i++)
        {
            int y = 26 + (i * 12);
            if (i == cursorIdx)
                u8g2.drawStr(0, y, ">");
            u8g2.drawStr(10, y, modes[i]);
        }
        break;
    }
    case SCR_SELECT_DURATION:
    {
        u8g2.drawStr(0, 10, "[ SET TEST DURATION ]");
        for (int i = 0; i < TOTAL_DURATION_PRESETS; i++)
        {
            int y = 21 + (i * 9);
            if (i == cursorIdx)
                u8g2.drawStr(0, y, ">");
            u8g2.setCursor(10, y);
            if (durationPresetsMin[i] == 0)
            {
                u8g2.print("Manual (No Limit)");
            }
            else
            {
                u8g2.print(durationPresetsMin[i]);
                u8g2.print(" Minutes");
                if (durationPresetsMin[i] == 15)
                    u8g2.print(" (Def)");
            }
        }
        break;
    }
    case SCR_TEST_RUNNING:
    {
        float livePres = readLivePressureBar();
        if (livePres > testPeakPressure)
            testPeakPressure = livePres;
        uint32_t elapsedSec = (millis() - testStartTime) / 1000UL;
        if (targetDurationSec > 0 && elapsedSec >= targetDurationSec)
        {
            finishAndSaveTest(targetDurationSec);
            return;
        }
        u8g2.setCursor(0, 10);
        if (targetDurationSec > 0)
        {
            uint32_t rem = targetDurationSec - elapsedSec;
            u8g2.print("[ LEFT: ");
            u8g2.print(rem / 60);
            u8g2.print("m ");
            u8g2.print(rem % 60);
            u8g2.print("s ]");
        }
        else
        {
            u8g2.print("[ TIME: ");
            u8g2.print(elapsedSec / 60);
            u8g2.print("m ");
            u8g2.print(elapsedSec % 60);
            u8g2.print("s ]");
        }
        u8g2.setCursor(0, 24);
        u8g2.print("Pres: ");
        u8g2.print(livePres, 2);
        u8g2.print(" Bar");
        u8g2.setCursor(0, 36);
        u8g2.print("Vol : ");
        u8g2.print(getTotalVolumeLiters(), 2);
        u8g2.print(" L");
        u8g2.setCursor(0, 48);
        u8g2.print("Flow: ");
        u8g2.print(getLiveFlowRateLPM(), 1);
        u8g2.print(" L/m");
        u8g2.drawStr(0, 62, "SEL:Save  BACK:Abort");
        break;
    }
    case SCR_NETWORK_MENU:
    {
        const char *netItems[] = {
            "Sync Pending Logs",
            "Select Gateway",
            "Setup Wi-Fi (Scan)",
            "Connection Test"};
        u8g2.setCursor(0, 10);
        u8g2.print("[ NETWORK (");
        u8g2.print(getPendingSyncCount());
        u8g2.print(" Pend) ]");
        for (int i = 0; i < 4; i++)
        {
            int y = 23 + (i * 10);
            if (i == cursorIdx)
                u8g2.drawStr(0, y, ">");
            u8g2.drawStr(10, y, netItems[i]);
        }
        break;
    }
    case SCR_GATEWAY_SELECT:
    {
        u8g2.drawStr(0, 10, "[ SELECT GATEWAY ]");
        u8g2.drawStr(0, 28, cursorIdx == 0 ? ">" : " ");
        u8g2.drawStr(10, 28, "Wi-Fi Client");
        u8g2.drawStr(0, 42, cursorIdx == 1 ? ">" : " ");
        u8g2.drawStr(10, 42, "Bluetooth Client");
        break;
    }
    case SCR_WIFI_SCAN_LIST:
    {
        u8g2.setCursor(0, 10);
        u8g2.print("[ WIFI FOUND: ");
        u8g2.print(scannedNetCount);
        u8g2.print(" ]");
        int startIdx = (cursorIdx / 4) * 4;
        for (int i = 0; i < 4; i++)
        {
            int netIdx = startIdx + i;
            if (netIdx >= scannedNetCount)
                break;
            int y = 23 + (i * 10);
            if (netIdx == cursorIdx)
                u8g2.drawStr(0, y, ">");
            String ssid = getScannedSSID(netIdx);
            if (ssid.length() > 14)
                ssid = ssid.substring(0, 14);
            u8g2.setCursor(10, y);
            u8g2.print(ssid);
            u8g2.print(" ");
            u8g2.print(getScannedRSSI(netIdx));
        }
        break;
    }
    case SCR_VIRTUAL_KEYBOARD:
    {
        u8g2.setCursor(0, 10);
        if (kbPurpose == KB_FOR_WIFI_PASS)
        {
            String shortSSID = (selectedWiFiSSID.length() > 15) ? selectedWiFiSSID.substring(0, 15) : selectedWiFiSSID;
            u8g2.print("Pass: ");
            u8g2.print(shortSSID);
        }
        else
        {
            u8g2.print("[ ENTER SITE ADDRESS ]");
        }
        u8g2.drawFrame(0, 14, 128, 15);
        String visibleText = (kbInputBuffer.length() > 17)
                                 ? kbInputBuffer.substring(kbInputBuffer.length() - 17)
                                 : kbInputBuffer;
        u8g2.setCursor(4, 25);
        u8g2.print(visibleText);
        u8g2.print("_");
        u8g2.drawStr(0, 44, "Pick:");
        int prevIdx = (keyCharIndex <= 0) ? KB_CHAR_LEN : keyCharIndex - 1;
        int nextIdx = (keyCharIndex >= KB_CHAR_LEN) ? 0 : keyCharIndex + 1;
        auto formatKey = [](int idx) -> String
        {
            if (idx == KB_CHAR_LEN)
                return "[OK]";
            if (kbChars[idx] == ' ')
                return "[SP]";
            return String(kbChars[idx]);
        };
        u8g2.setCursor(36, 44);
        u8g2.print(formatKey(prevIdx));
        u8g2.drawBox(64, 33, 30, 14);
        u8g2.setDrawColor(0);
        u8g2.setCursor(68, 44);
        u8g2.print(formatKey(keyCharIndex));
        u8g2.setDrawColor(1);
        u8g2.setCursor(100, 44);
        u8g2.print(formatKey(nextIdx));
        u8g2.drawStr(0, 61, "SEL:Add  BACK:Del");
        break;
    }
    case SCR_CONN_TEST:
    {
        u8g2.drawStr(0, 10, "[ CONNECTION TEST ]");
        u8g2.setCursor(0, 26);
        u8g2.print("Mode: ");
        u8g2.print(getActiveGatewayMode() == MODE_WIFI ? "Wi-Fi" : "Bluetooth");
        u8g2.setCursor(0, 38);
        u8g2.print(getNetworkStatusText());
        if (getActiveGatewayMode() == MODE_WIFI && getSavedWiFiSSID().length() > 0)
        {
            u8g2.setCursor(0, 50);
            u8g2.print("SSID: ");
            u8g2.print(getSavedWiFiSSID());
        }
        u8g2.drawStr(0, 62, "Press BACK to return");
        break;
    }
    case SCR_STORAGE_MENU:
    {
        u8g2.setCursor(0, 10);
        u8g2.print("[ DATA: ");
        u8g2.print(getStoredLogCount());
        u8g2.print("/");
        u8g2.print(MAX_STORED_LOGS);
        u8g2.print(" ]");
        u8g2.drawStr(0, 28, cursorIdx == 0 ? ">" : " ");
        u8g2.drawStr(10, 28, "View Saved Logs");
        u8g2.drawStr(0, 42, cursorIdx == 1 ? ">" : " ");
        u8g2.drawStr(10, 42, "Clear All Memory");
        break;
    }
    case SCR_VIEW_LOGS:
    {
        TestLog log;
        if (readTestLogByIndex((uint16_t)logViewIndex, log))
        {
            u8g2.setCursor(0, 10);
            u8g2.print("#");
            u8g2.print(log.logId);
            u8g2.print("[");
            u8g2.print(log.syncStatus);
            u8g2.print("] ");
            String shortSite = String(log.siteAddress);
            if (shortSite.length() > 11)
                shortSite = shortSite.substring(0, 11);
            u8g2.print(shortSite);
            u8g2.setCursor(0, 24);
            u8g2.print("Vol : ");
            u8g2.print(log.totalVolumeLiters, 2);
            u8g2.print(" L");
            u8g2.setCursor(0, 36);
            u8g2.print("Peak: ");
            u8g2.print(log.peakPressureBar, 2);
            u8g2.print(" Bar");
            u8g2.setCursor(0, 48);
            u8g2.print("Time: ");
            u8g2.print(log.durationSec);
            u8g2.print(" sec");
            u8g2.drawStr(0, 62, "UP/DN:Nav SEL:Del");
        }
        break;
    }
    case SCR_CLEAR_CONFIRM:
    {
        u8g2.drawStr(0, 12, "Erase ALL stored logs?");
        u8g2.drawStr(0, 30, cursorIdx == 0 ? "> YES, Erase All" : "  YES, Erase All");
        u8g2.drawStr(0, 44, cursorIdx == 1 ? "> NO, Cancel" : "  NO, Cancel");
        break;
    }
    case SCR_TOOLS_MENU:
    {
        u8g2.drawStr(0, 10, "[ HARDWARE TOOLS ]");
        u8g2.drawStr(0, 22, cursorIdx == 0 ? ">" : " ");
        u8g2.setCursor(10, 22);
        u8g2.print(getSolenoidState() ? "Valve: OPEN (Toggle)" : "Valve: CLOSED (Toggle)");
        u8g2.drawStr(0, 32, cursorIdx == 1 ? ">" : " ");
        u8g2.drawStr(10, 32, "Zero Pressure Sensor");
        u8g2.drawStr(0, 42, cursorIdx == 2 ? ">" : " ");
        u8g2.drawStr(10, 42, "Test Buzzer / OLED");
        u8g2.drawStr(0, 52, cursorIdx == 3 ? ">" : " ");
        u8g2.drawStr(10, 52, "Flow Calibration");
        break;
    }
    case SCR_CALIBRATE_FLOW:
    {
        u8g2.drawStr(0, 10, "[ FLOW K-FACTOR ]");
        u8g2.setCursor(10, 32);
        u8g2.print("K = ");
        u8g2.print(getFlowKFactor(), 2);
        u8g2.drawStr(0, 50, "UP/DN:Adj  SEL:Save");
        break;
    }
    case SCR_OLED_TEST:
    {
        u8g2.drawBox(0, 0, 128, 64);
        break;
    }
    case SCR_DEVICE_STATUS:
    {
        float v = getBatteryVoltage();
        int pct = getBatteryPercentage(v);
        u8g2.drawStr(0, 10, "[ DEVICE STATUS ]");
        u8g2.setCursor(0, 26);
        u8g2.print("Bat: ");
        u8g2.print(v, 1);
        u8g2.print("V (");
        u8g2.print(pct);
        u8g2.print("%)");
        u8g2.setCursor(0, 38);
        u8g2.print("FW : ");
        u8g2.print(FIRMWARE_VERSION);
        u8g2.drawStr(0, 58, "SEL: Shut Down (Sleep)");
        break;
    }
    }
    if (millis() < toastExpireTime && toastMsg.length() > 0)
    {
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, 51, 128, 13);
        u8g2.setDrawColor(0);
        u8g2.drawStr(4, 61, toastMsg.c_str());
        u8g2.setDrawColor(1);
    }
    u8g2.sendBuffer();
}