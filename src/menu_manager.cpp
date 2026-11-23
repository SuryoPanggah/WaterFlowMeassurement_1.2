#include "menu_manager.h"
#include "config.h"
#include "power_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "network_manager.h"
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;

// =========================================================================
// COLOR PALETTE (RGB565 Dark Industrial Theme for 320x240 TFT)
// =========================================================================
#define COL_BG 0x0841     // Deep Slate
#define COL_HEADER 0x10A2 // Header Bar
#define COL_CARD 0x18E3   // Card Fill
#define COL_ACCENT 0x05BF // Sky Blue
#define COL_TEXT 0xFFFF   // Pure White
#define COL_MUTED 0x9CD3  // Cool Grey
#define COL_GREEN 0x2668  // Emerald Green
#define COL_RED 0xE186    // Alert Red
#define COL_AMBER 0xFD20  // Warning Amber

// =========================================================================
// MENU STATE MACHINE DEFINITIONS
// =========================================================================
enum ScreenState
{
    SCR_MAIN_MENU,
    SCR_SITE_SETUP,
    SCR_SD_SITE_PICKER,
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
    SCR_CALIBRATE_MAX_PRES,
    SCR_TFT_TEST,
    SCR_DEVICE_STATUS
};

enum KeyboardPurpose
{
    KB_FOR_WIFI_PASS,
    KB_FOR_SITE_ADDR
};

static ScreenState currentScreen = SCR_MAIN_MENU;
static int cursorIdx = 0;
static bool needsFullRedraw = true;
static unsigned long lastLiveDrawMs = 0;

// Temporary popup toast banner state
static String toastMsg = "";
static unsigned long toastExpireTime = 0;
static bool toastWasVisible = false;

// Two-way synchronized UI Language State (false = English [default], true = Bahasa Indonesia)
static bool langIsIndonesian = false;

bool isLanguageIndonesian() {
    return langIsIndonesian;
}

void setLanguageIndonesian(bool isId) {
    if (langIsIndonesian != isId) {
        langIsIndonesian = isId;
        needsFullRedraw = true;
    }
}

// Helper: returns English or Bahasa Indonesia text based on active language
static inline const char* TR(const char* en, const char* id) {
    return langIsIndonesian ? id : en;
}


// Active Site & Test Tracking Variables
static String activeSiteAddress = "";
static uint8_t activeTestMode = 0;
static uint32_t targetDurationSec = 900;
static unsigned long testStartTime = 0;
static float testPeakPressure = 0.0f;

static const uint16_t durationPresetsMin[] = {15, 10, 5, 1, 0};
static const int TOTAL_DURATION_PRESETS = 5;

// Log Viewer & SD Site Picker Scroll Indices
static int logViewIndex = 0;
static int sdSiteIndex = 0;

// Wi-Fi Scanner & Reusable Virtual Keyboard State
static int scannedNetCount = 0;
static String selectedWiFiSSID = "";
static KeyboardPurpose kbPurpose = KB_FOR_SITE_ADDR;
static String kbInputBuffer = "";
static int kbMaxLen = 23;
static int keyCharIndex = 0;

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
    needsFullRedraw = true;
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
        showToast(isSDCardReady() ? "Saved to FRAM + SD!" : "Test Saved to FRAM!");
    else if (res == SAVE_OVERWROTE_SYNCED)
        showToast("Saved (Old Synced Replaced)");
    else if (res == SAVE_OVERWROTE_PENDING)
        showToast("Saved (Oldest Overwritten)");
    else
        showToast("FRAM Save Error!");

    beepBuzzer(150);
    changeScreen(SCR_MAIN_MENU, 0);
}

// =========================================================================
// TFT DRAWING HELPERS
// =========================================================================
static void drawHeader(const String &title)
{
    tft.fillRect(0, 0, 320, 32, COL_HEADER);
    tft.drawFastHLine(0, 32, 320, COL_ACCENT);
    tft.setTextColor(COL_ACCENT, COL_HEADER);
    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.print(title);

    // Top-right SD & Battery status badge
    float v = getBatteryVoltage();
    tft.setTextSize(1);
    tft.setTextColor(isSDCardReady() ? COL_GREEN : COL_MUTED, COL_HEADER);
    tft.setCursor(220, 6);
    tft.print(isSDCardReady() ? "[SD OK]" : "[NO SD]");
    tft.setTextColor(COL_TEXT, COL_HEADER);
    tft.setCursor(220, 18);
    tft.print(v, 1);
    tft.print("V ");
    tft.print(getBatteryPercentage(v));
    tft.print("%");
}

static void drawFooter(const String &hint)
{
    tft.fillRect(0, 218, 320, 22, COL_HEADER);
    tft.setTextColor(COL_MUTED, COL_HEADER);
    tft.setTextSize(1);
    tft.setCursor(10, 225);
    tft.print(hint);
}

static void drawMenuItem(int row, const String &label, bool selected)
{
    int y = 42 + (row * 32);
    uint16_t bg = selected ? COL_ACCENT : COL_CARD;
    uint16_t fg = selected ? COL_BG : COL_TEXT;
    tft.fillRoundRect(10, y, 300, 27, 5, bg);
    tft.setTextColor(fg, bg);
    tft.setTextSize(2);
    tft.setCursor(20, y + 6);
    tft.print(selected ? "> " : "  ");
    tft.print(label);
}

void initMenuManager()
{
    currentScreen = SCR_MAIN_MENU;
    cursorIdx = 0;
    needsFullRedraw = true;
}

// =========================================================================
// 4-BUTTON INPUT HANDLER
// =========================================================================
void handleMenuInput(bool upPressed, bool downPressed, bool selectPressed, bool backPressed)
{
    if (!upPressed && !downPressed && !selectPressed && !backPressed)
        return;
    toastExpireTime = 0;
    needsFullRedraw = true;

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
                changeScreen(SCR_SITE_SETUP, activeSiteAddress.length() > 0 ? 0 : 1);
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

    // --- 2. SITE SETUP -> SD PICKER / KEYBOARD -> MODE -> DURATION ---
    if (currentScreen == SCR_SITE_SETUP)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 0);
            return;
        }
        if (upPressed)
            cursorIdx = (cursorIdx <= 0) ? 2 : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= 2) ? 0 : cursorIdx + 1;
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                if (activeSiteAddress.length() == 0)
                    showToast("Set Site Address First!");
                else
                    changeScreen(SCR_SELECT_MODE, 0);
            }
            else if (cursorIdx == 1)
            {
                uint16_t count = getPreloadedSiteCount();
                if (count == 0)
                    count = reloadSitesFromSD();
                if (count == 0)
                    showToast("No sites.csv on SD!");
                else
                {
                    sdSiteIndex = 0;
                    changeScreen(SCR_SD_SITE_PICKER, 0);
                }
            }
            else
            {
                openKeyboard(KB_FOR_SITE_ADDR, activeSiteAddress, 23);
            }
        }
        return;
    }

    if (currentScreen == SCR_SD_SITE_PICKER)
    {
        uint16_t total = getPreloadedSiteCount();
        if (backPressed || total == 0)
        {
            changeScreen(SCR_SITE_SETUP, 1);
            return;
        }
        if (upPressed)
            sdSiteIndex = (sdSiteIndex <= 0) ? (total - 1) : sdSiteIndex - 1;
        if (downPressed)
            sdSiteIndex = (sdSiteIndex >= (int)total - 1) ? 0 : sdSiteIndex + 1;
        if (selectPressed)
        {
            activeSiteAddress = getPreloadedSite((uint16_t)sdSiteIndex);
            showToast("Selected: " + activeSiteAddress);
            changeScreen(SCR_SELECT_MODE, 0);
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

    // --- 4. NETWORK / SYNC MENU ---
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
                tft.fillScreen(COL_BG);
                drawHeader("WIFI SCANNER");
                tft.setTextColor(COL_TEXT, COL_BG);
                tft.setTextSize(2);
                tft.setCursor(45, 110);
                tft.print("Scanning Wi-Fi...");
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
                        showToast("Address Cannot Be Empty");
                    else
                    {
                        activeSiteAddress = kbInputBuffer;
                        showToast("Site Address Set!");
                        changeScreen(SCR_SELECT_MODE, 0);
                    }
                }
            }
            else if ((int)kbInputBuffer.length() < kbMaxLen)
            {
                kbInputBuffer += kbChars[keyCharIndex];
            }
        }
        if (backPressed)
        {
            if (kbInputBuffer.length() > 0)
                kbInputBuffer.remove(kbInputBuffer.length() - 1);
            else
                changeScreen(kbPurpose == KB_FOR_WIFI_PASS ? SCR_NETWORK_MENU : SCR_SITE_SETUP, 2);
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
            uint16_t rem = getStoredLogCount();
            if (rem == 0)
                changeScreen(SCR_STORAGE_MENU, 0);
            else if (logViewIndex >= rem)
                logViewIndex = rem - 1;
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
                showToast("FRAM Logs Cleared");
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
            cursorIdx = (cursorIdx <= 0) ? 4 : cursorIdx - 1;
        if (downPressed)
            cursorIdx = (cursorIdx >= 4) ? 0 : cursorIdx + 1;
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
                saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset(), getMaxPressureBar());
                showToast("Pressure Zeroed!");
            }
            else if (cursorIdx == 2)
            {
                beepBuzzer(150);
                changeScreen(SCR_TFT_TEST);
            }
            else if (cursorIdx == 3)
            {
                changeScreen(SCR_CALIBRATE_FLOW);
            }
            else if (cursorIdx == 4)
            {
                changeScreen(SCR_CALIBRATE_MAX_PRES);
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
            saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset(), getMaxPressureBar());
            showToast("K-Factor Saved!");
            changeScreen(SCR_TOOLS_MENU, 3);
        }
        if (backPressed)
            changeScreen(SCR_TOOLS_MENU, 3);
        return;
    }
    if (currentScreen == SCR_CALIBRATE_MAX_PRES)
    {
        float maxBar = getMaxPressureBar();
        if (upPressed)
            setMaxPressureBar(maxBar + 0.5f);
        if (downPressed)
            setMaxPressureBar(maxBar - 0.5f);
        if (selectPressed)
        {
            saveCalibrationToFRAM(getFlowKFactor(), getPressureZeroOffset(), getMaxPressureBar());
            showToast("Max Pressure Saved!");
            changeScreen(SCR_TOOLS_MENU, 4);
        }
        if (backPressed)
            changeScreen(SCR_TOOLS_MENU, 4);
        return;
    }
    if (currentScreen == SCR_TFT_TEST)
    {
        if (backPressed || selectPressed)
            changeScreen(SCR_TOOLS_MENU, 2);
        return;
    }
    // --- 7. DEVICE SETTINGS & DEEP SLEEP ---
    if (currentScreen == SCR_DEVICE_STATUS)
    {
        if (backPressed)
        {
            changeScreen(SCR_MAIN_MENU, 4);
            return;
        }
        if (upPressed || downPressed)
        {
            cursorIdx = (cursorIdx == 0) ? 1 : 0;
            return;
        }
        if (selectPressed)
        {
            if (cursorIdx == 0)
            {
                setLanguageIndonesian(!langIsIndonesian);
                showToast(TR("Language: English", "Bahasa: Indonesia"));
            }
            else
            {
                enterDeepSleep();
            }
        }
        return;
    }
}
// =========================================================================
// 2.8" ILI9341 TFT SCREEN RENDERING ENGINE
// =========================================================================
void renderMenu()
{
    bool isLiveScreen = (currentScreen == SCR_TEST_RUNNING || currentScreen == SCR_CONN_TEST || currentScreen == SCR_DEVICE_STATUS);
    bool toastActive = (millis() < toastExpireTime && toastMsg.length() > 0);
    if (toastWasVisible && !toastActive)
    {
        needsFullRedraw = true;
    }
    toastWasVisible = toastActive;
    if (!needsFullRedraw && !isLiveScreen && !toastActive)
        return;
    if (!needsFullRedraw && isLiveScreen && (millis() - lastLiveDrawMs < 250))
        return;
    lastLiveDrawMs = millis();
    if (needsFullRedraw && currentScreen != SCR_TFT_TEST)
    {
        tft.fillScreen(COL_BG);
    }
    switch (currentScreen)
    {
    case SCR_MAIN_MENU:
    {
        if (needsFullRedraw)
        {
            drawHeader("MAIN MENU");
            const char *items[] = {
                "1. Start Field Test",
                "2. Network / Sync",
                "3. Stored Data",
                "4. Hardware Tools",
                "5. Device Status"};
            for (int i = 0; i < 5; i++)
                drawMenuItem(i, items[i], i == cursorIdx);
            drawFooter("UP/DN: Move   SEL: Open");
        }
        break;
    }
    case SCR_SITE_SETUP:
    {
        if (needsFullRedraw)
        {
            drawHeader("SITE LOCATION");
            tft.fillRoundRect(10, 40, 300, 34, 5, COL_CARD);
            tft.setTextColor(COL_MUTED, COL_CARD);
            tft.setTextSize(1);
            tft.setCursor(18, 46);
            tft.print("ACTIVE SITE:");
            tft.setTextColor(COL_ACCENT, COL_CARD);
            tft.setTextSize(2);
            tft.setCursor(18, 56);
            tft.print(activeSiteAddress.length() > 0 ? activeSiteAddress : "(Not Set)");
            String sdLabel = "Pick from SD (" + String(getPreloadedSiteCount()) + " sites)";
            const char *opts[] = {"Use Current Site", sdLabel.c_str(), "Type New Address"};
            for (int i = 0; i < 3; i++)
            {
                int y = 84 + (i * 36);
                uint16_t bg = (i == cursorIdx) ? COL_ACCENT : COL_CARD;
                uint16_t fg = (i == cursorIdx) ? COL_BG : COL_TEXT;
                tft.fillRoundRect(10, y, 300, 30, 5, bg);
                tft.setTextColor(fg, bg);
                tft.setTextSize(2);
                tft.setCursor(20, y + 7);
                tft.print(i == cursorIdx ? "> " : "  ");
                tft.print(opts[i]);
            }
            drawFooter("SEL: Choose   BACK: Return");
        }
        break;
    }
    case SCR_SD_SITE_PICKER:
    {
        if (needsFullRedraw)
        {
            uint16_t total = getPreloadedSiteCount();
            drawHeader("SD SITES (" + String(sdSiteIndex + 1) + "/" + String(total) + ")");
            int startIdx = (sdSiteIndex / 5) * 5;
            for (int i = 0; i < 5; i++)
            {
                int idx = startIdx + i;
                if (idx >= (int)total)
                    break;
                drawMenuItem(i, getPreloadedSite((uint16_t)idx), idx == sdSiteIndex);
            }
            drawFooter("UP/DN: Scroll   SEL: Select Site");
        }
        break;
    }
    case SCR_SELECT_MODE:
    {
        if (needsFullRedraw)
        {
            drawHeader("SELECT TEST MODE");
            const char *modes[] = {"Full Test (Pres + Vol)", "Pressure Only (Closed)", "Volume Only (Open)"};
            for (int i = 0; i < 3; i++)
                drawMenuItem(i, modes[i], i == cursorIdx);
            drawFooter("SEL: Confirm   BACK: Return");
        }
        break;
    }
    case SCR_SELECT_DURATION:
    {
        if (needsFullRedraw)
        {
            drawHeader("TEST DURATION");
            for (int i = 0; i < TOTAL_DURATION_PRESETS; i++)
            {
                String lbl = (durationPresetsMin[i] == 0)
                                 ? "Manual (No Time Limit)"
                                 : String(durationPresetsMin[i]) + " Minutes" + (durationPresetsMin[i] == 15 ? " (Default)" : "");
                drawMenuItem(i, lbl, i == cursorIdx);
            }
            drawFooter("SEL: START TEST   BACK: Return");
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
        if (needsFullRedraw)
        {
            drawHeader("TEST RUNNING");
            tft.fillRoundRect(10, 42, 145, 76, 6, COL_CARD);
            tft.fillRoundRect(165, 42, 145, 76, 6, COL_CARD);
            tft.fillRoundRect(10, 128, 145, 76, 6, COL_CARD);
            tft.fillRoundRect(165, 128, 145, 76, 6, COL_CARD);
            tft.setTextSize(1);
            tft.setTextColor(COL_MUTED, COL_CARD);
            tft.setCursor(20, 50);
            tft.print("PRESSURE (BAR)");
            tft.setCursor(175, 50);
            tft.print("VOLUME (LITERS)");
            tft.setCursor(20, 136);
            tft.print("FLOW RATE (L/MIN)");
            tft.setCursor(175, 136);
            tft.print(targetDurationSec > 0 ? "TIME REMAINING" : "ELAPSED TIME");
            drawFooter("SEL: STOP & SAVE    BACK: ABORT");
        }
        tft.setTextSize(3);
        tft.setTextColor(COL_ACCENT, COL_CARD);
        tft.setCursor(20, 72);
        tft.print(livePres, 2);
        tft.print("  ");
        tft.setTextColor(COL_GREEN, COL_CARD);
        tft.setCursor(175, 72);
        tft.print(getTotalVolumeLiters(), 2);
        tft.print("  ");
        tft.setTextColor(COL_TEXT, COL_CARD);
        tft.setCursor(20, 158);
        tft.print(getLiveFlowRateLPM(), 1);
        tft.print("  ");
        uint32_t dispSec = (targetDurationSec > 0) ? (targetDurationSec - elapsedSec) : elapsedSec;
        char timeBuf[12];
        snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", dispSec / 60, dispSec % 60);
        tft.setTextColor(COL_AMBER, COL_CARD);
        tft.setCursor(175, 158);
        tft.print(timeBuf);
        break;
    }
    case SCR_NETWORK_MENU:
    {
        if (needsFullRedraw)
        {
            drawHeader("NETWORK (" + String(getPendingSyncCount()) + " PEND)");
            const char *netItems[] = {
                "Sync Pending Logs",
                "Select Gateway Mode",
                "Setup Wi-Fi (Scan)",
                "Connection Status"};
            for (int i = 0; i < 4; i++)
                drawMenuItem(i, netItems[i], i == cursorIdx);
            drawFooter("SEL: Open   BACK: Main Menu");
        }
        break;
    }
    case SCR_GATEWAY_SELECT:
    {
        if (needsFullRedraw)
        {
            drawHeader("SELECT GATEWAY");
            drawMenuItem(0, "Wi-Fi Client (Web UI)", cursorIdx == 0);
            drawMenuItem(1, "Bluetooth Classic SPP", cursorIdx == 1);
            drawFooter("Note: Only 1 radio active at a time");
        }
        break;
    }
    case SCR_WIFI_SCAN_LIST:
    {
        if (needsFullRedraw)
        {
            drawHeader("WIFI FOUND: " + String(scannedNetCount));
            int startIdx = (cursorIdx / 5) * 5;
            for (int i = 0; i < 5; i++)
            {
                int netIdx = startIdx + i;
                if (netIdx >= scannedNetCount)
                    break;
                String row = getScannedSSID(netIdx) + " (" + String(getScannedRSSI(netIdx)) + ")";
                drawMenuItem(i, row, netIdx == cursorIdx);
            }
            drawFooter("SEL: Enter Password   BACK: Cancel");
        }
        break;
    }
    case SCR_VIRTUAL_KEYBOARD:
    {
        if (needsFullRedraw)
        {
            drawHeader(kbPurpose == KB_FOR_WIFI_PASS ? "WIFI PASSWORD" : "ENTER SITE ADDRESS");
            tft.drawRoundRect(10, 44, 300, 40, 5, COL_ACCENT);
            tft.fillRect(14, 48, 292, 32, COL_CARD);
            tft.setTextColor(COL_TEXT, COL_CARD);
            tft.setTextSize(2);
            tft.setCursor(20, 56);
            tft.print(kbInputBuffer);
            tft.print("_");
            int prevIdx = (keyCharIndex <= 0) ? KB_CHAR_LEN : keyCharIndex - 1;
            int nextIdx = (keyCharIndex >= KB_CHAR_LEN) ? 0 : keyCharIndex + 1;
            auto fmtKey = [](int idx) -> String
            {
                if (idx == KB_CHAR_LEN)
                    return "[OK]";
                if (kbChars[idx] == ' ')
                    return "[SP]";
                return String(kbChars[idx]);
            };
            tft.setTextSize(2);
            tft.setTextColor(COL_MUTED, COL_BG);
            tft.setCursor(55, 118);
            tft.print(fmtKey(prevIdx));
            tft.fillRoundRect(120, 105, 80, 42, 6, COL_ACCENT);
            tft.setTextColor(COL_BG, COL_ACCENT);
            tft.setCursor(138, 118);
            tft.print(fmtKey(keyCharIndex));
            tft.setTextColor(COL_MUTED, COL_BG);
            tft.setCursor(235, 118);
            tft.print(fmtKey(nextIdx));
            drawFooter("UP/DN: Char  SEL: Add/OK  BACK: Del");
        }
        break;
    }
    case SCR_CONN_TEST:
    {
        if (needsFullRedraw)
        {
            drawHeader("CONNECTION STATUS");
            drawFooter("Press BACK to return");
        }
        tft.fillRoundRect(10, 45, 300, 150, 6, COL_CARD);
        tft.setTextColor(COL_TEXT, COL_CARD);
        tft.setTextSize(2);
        tft.setCursor(22, 60);
        tft.print("Mode: ");
        tft.print(getActiveGatewayMode() == MODE_WIFI ? "Wi-Fi" : "Bluetooth");
        tft.setTextColor(isNetworkConnected() ? COL_GREEN : COL_AMBER, COL_CARD);
        tft.setCursor(22, 95);
        tft.print(getNetworkStatusText());
        if (getActiveGatewayMode() == MODE_WIFI && isNetworkConnected())
        {
            tft.setTextColor(COL_ACCENT, COL_CARD);
            tft.setCursor(22, 130);
            tft.print("IP: ");
            tft.print(WiFi.localIP());
            tft.setTextSize(1);
            tft.setTextColor(COL_MUTED, COL_CARD);
            tft.setCursor(22, 162);
            tft.print("Open http://water-rig.local on phone");
        }
        break;
    }
    case SCR_STORAGE_MENU:
    {
        if (needsFullRedraw)
        {
            drawHeader("DATA (" + String(getStoredLogCount()) + "/" + String(MAX_STORED_LOGS) + ")");
            drawMenuItem(0, "View FRAM Logs", cursorIdx == 0);
            drawMenuItem(1, "Clear FRAM Memory", cursorIdx == 1);
            drawFooter(isSDCardReady() ? "microSD Backup: ACTIVE" : "microSD Backup: NO CARD");
        }
        break;
    }
    case SCR_VIEW_LOGS:
    {
        if (needsFullRedraw)
        {
            TestLog log;
            if (readTestLogByIndex((uint16_t)logViewIndex, log))
            {
                drawHeader("LOG #" + String(log.logId) + " [" + String(log.syncStatus) + "]");
                tft.fillRoundRect(10, 44, 300, 155, 6, COL_CARD);
                tft.setTextColor(COL_ACCENT, COL_CARD);
                tft.setTextSize(2);
                tft.setCursor(20, 56);
                tft.print(log.siteAddress);
                tft.setTextColor(COL_TEXT, COL_CARD);
                tft.setCursor(20, 92);
                tft.print("Volume : ");
                tft.print(log.totalVolumeLiters, 2);
                tft.print(" L");
                tft.setCursor(20, 124);
                tft.print("Peak P : ");
                tft.print(log.peakPressureBar, 2);
                tft.print(" Bar");
                tft.setCursor(20, 156);
                tft.print("Time   : ");
                tft.print(log.durationSec);
                tft.print(" sec");
            }
            drawFooter("UP/DN: Next/Prev   SEL: Delete");
        }
        break;
    }
    case SCR_CLEAR_CONFIRM:
    {
        if (needsFullRedraw)
        {
            drawHeader("CONFIRM ERASE");
            tft.setTextColor(COL_RED, COL_BG);
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Erase ALL FRAM logs?");
            drawMenuItem(1, "YES, Erase All Logs", cursorIdx == 0);
            drawMenuItem(2, "NO, Cancel", cursorIdx == 1);
        }
        break;
    }
    case SCR_TOOLS_MENU:
    {
        if (needsFullRedraw)
        {
            drawHeader("HARDWARE TOOLS");
            const char *tools[] = {
                getSolenoidState() ? "Valve: OPEN (Toggle)" : "Valve: CLOSED (Toggle)",
                "Zero Pressure Sensor",
                "Test Buzzer & TFT",
                "Flow K-Factor Calib",
                "Max Pressure Rating"};
            for (int i = 0; i < 5; i++)
                drawMenuItem(i, tools[i], i == cursorIdx);
            drawFooter("SEL: Execute   BACK: Main Menu");
        }
        break;
    }
    case SCR_CALIBRATE_FLOW:
    {
        if (needsFullRedraw)
        {
            drawHeader("FLOW K-FACTOR");
            tft.fillRoundRect(20, 60, 280, 95, 8, COL_CARD);
            tft.setTextColor(COL_MUTED, COL_CARD);
            tft.setTextSize(2);
            tft.setCursor(45, 75);
            tft.print("YF-B1 K-Factor:");
            tft.setTextColor(COL_ACCENT, COL_CARD);
            tft.setTextSize(3);
            tft.setCursor(85, 112);
            tft.print(getFlowKFactor(), 2);
            drawFooter("UP/DN: +/- 0.1   SEL: Save to FRAM");
        }
        break;
    }
    case SCR_CALIBRATE_MAX_PRES:
    {
        if (needsFullRedraw)
        {
            drawHeader("MAX PRESSURE RATING");
            tft.fillRoundRect(20, 60, 280, 95, 8, COL_CARD);
            tft.setTextColor(COL_MUTED, COL_CARD);
            tft.setTextSize(2);
            tft.setCursor(45, 75);
            tft.print("Sensor Full Scale:");
            tft.setTextColor(COL_ACCENT, COL_CARD);
            tft.setTextSize(3);
            tft.setCursor(70, 112);
            tft.print(getMaxPressureBar(), 1);
            tft.print(" Bar");
            drawFooter("UP/DN: +/- 0.5 Bar   SEL: Save");
        }
        break;
    }
    case SCR_TFT_TEST:
    {
        if (needsFullRedraw)
        {
            tft.fillScreen(ILI9341_WHITE);
            tft.setTextColor(ILI9341_BLACK, ILI9341_WHITE);
            tft.setTextSize(2);
            tft.setCursor(45, 110);
            tft.print("TFT & BUZZER OK");
        }
        break;
    }
    case SCR_DEVICE_STATUS:
    {
        if (needsFullRedraw)
        {
            drawHeader("DEVICE STATUS");
            drawFooter("SEL: SHUT DOWN (DEEP SLEEP)");
        }
        float v = getBatteryVoltage();
        tft.fillRoundRect(10, 45, 300, 155, 6, COL_CARD);
        tft.setTextColor(COL_TEXT, COL_CARD);
        tft.setTextSize(2);
        tft.setCursor(22, 60);
        tft.print("Battery : ");
        tft.print(v, 2);
        tft.print("V (");
        tft.print(getBatteryPercentage(v));
        tft.print("%)");
        tft.setCursor(22, 92);
        tft.print("microSD : ");
        tft.print(isSDCardReady() ? "Mounted" : "Not Found");
        tft.setCursor(22, 124);
        tft.print("Max Pres: ");
        tft.print(getMaxPressureBar(), 1);
        tft.print(" Bar");
        tft.setCursor(22, 156);
        tft.print("Firmware: ");
        tft.print(FIRMWARE_VERSION);

        // --- FUTURE PRODUCTION MULTI-RAIL DISPLAY (Uncomment with power_manager.cpp) ---
        /*
        float v12 = getBus12VVoltage();
        float v5  = getRail5VVoltage();

        tft.setTextColor(COL_ACCENT, COL_CARD);
        tft.setTextSize(2);
        tft.setCursor(20, 122);
        tft.print("Source : ");
        tft.print(getPowerSourceText());

        tft.setTextColor(COL_TEXT, COL_CARD);
        tft.setCursor(20, 150);
        tft.print("12V Bus: ");
        tft.print(v12, 1);
        tft.print("V  5V: ");
        tft.print(v5, 1);
        tft.print("V");
        */

        break;
    }
    }
    if (toastActive)
    {
        tft.fillRoundRect(20, 182, 280, 30, 6, COL_AMBER);
        tft.setTextColor(COL_BG, COL_AMBER);
        tft.setTextSize(2);
        tft.setCursor(32, 189);
        tft.print(toastMsg);
    }
    needsFullRedraw = false;
}
// =========================================================================
// WEB PORTAL REMOTE SYNCHRONIZATION HOOKS
// =========================================================================
void remoteSetSiteAddress(const String &site)
{
    if (site.length() > 0)
    {
        activeSiteAddress = site.substring(0, 23);
        showToast("Site Updated via Web");
        needsFullRedraw = true;
    }
}
String remoteGetSiteAddress()
{
    return activeSiteAddress;
}
bool remoteStartTest(uint8_t mode, uint16_t durationMinutes)
{
    if (currentScreen == SCR_TEST_RUNNING)
        return false;
    activeTestMode = (mode > 2) ? 0 : mode;
    targetDurationSec = (uint32_t)durationMinutes * 60UL;
    resetFlowCounters();
    testPeakPressure = 0.0f;
    testStartTime = millis();
    setSolenoidState(activeTestMode != 1);
    beepBuzzer(100);
    changeScreen(SCR_TEST_RUNNING);
    return true;
}
void remoteStopAndSaveTest()
{
    if (currentScreen == SCR_TEST_RUNNING)
    {
        uint32_t elapsedSec = (millis() - testStartTime) / 1000UL;
        finishAndSaveTest(elapsedSec);
    }
}
void remoteAbortTest()
{
    if (currentScreen == SCR_TEST_RUNNING)
    {
        setSolenoidState(false);
        showToast("Test Aborted via Web");
        changeScreen(SCR_MAIN_MENU, 0);
    }
}
bool remoteIsTestRunning()
{
    return (currentScreen == SCR_TEST_RUNNING);
}
uint32_t remoteGetRemainingOrElapsedSec()
{
    if (currentScreen != SCR_TEST_RUNNING)
        return 0;
    uint32_t elapsed = (millis() - testStartTime) / 1000UL;
    if (targetDurationSec > 0)
    {
        return (elapsed >= targetDurationSec) ? 0 : (targetDurationSec - elapsed);
    }
    return elapsed;
}