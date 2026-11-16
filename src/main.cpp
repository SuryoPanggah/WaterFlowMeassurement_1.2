#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "config.h"
#include "power_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "menu_manager.h"

// Global 128x64 I2C OLED display instance (referenced via 'extern' in power_manager & menu_manager)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);

// Button debouncing state trackers (active-low inputs)
static bool lastUpState = HIGH;
static bool lastDownState = HIGH;
static bool lastSelectState = HIGH;
static bool lastBackState = HIGH;
static unsigned long lastDebounceTime = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Water Audit Rig Booting ===");

    // 1. Initialize shared I2C bus for OLED & FRAM
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);

    // 2. Configure 4 physical UI buttons with internal pull-ups
    pinMode(BTN_UP_PIN, INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_SELECT_PIN, INPUT_PULLUP);
    pinMode(BTN_BACK_PIN, INPUT_PULLUP);

    // 3. Initialize OLED display and show boot splash screen
    u8g2.begin();
    u8g2.setPowerSave(0); // Ensure display wakes up cleanly after Deep Sleep
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(14, 28, "WATER AUDIT RIG");
    u8g2.setFont(u8g2_font_profont10_mf);
    u8g2.drawStr(36, 44, FIRMWARE_VERSION);
    u8g2.sendBuffer();

    // 4. Initialize hardware modules in safe dependency order
    initPowerManager();
    initSensorManager();

    // 5. Mount FRAM and restore saved calibration constants into sensor_manager
    if (initStorageManager()) {
        float savedK = DEFAULT_FLOW_K_FACTOR;
        float savedOffset = DEFAULT_PRESSURE_OFFSET;
        loadCalibrationFromFRAM(savedK, savedOffset);
        setFlowKFactor(savedK);
        setPressureZeroOffset(savedOffset);
    }

    // 6. Initialize Network (Wi-Fi / BT) and Menu State Machine
    initNetworkManager();
    initMenuManager();

    delay(800); // Brief pause so engineer sees boot splash
    beepBuzzer(60);
    Serial.println("=== System Ready ===");
}

void loop() {
    // 1. Update background sensor math (flow rate) and network commands
    updateSensors();
    updateNetwork();

    // 2. Read raw button pins (LOW = pressed)
    bool currUp     = digitalRead(BTN_UP_PIN);
    bool currDown   = digitalRead(BTN_DOWN_PIN);
    bool currSelect = digitalRead(BTN_SELECT_PIN);
    bool currBack   = digitalRead(BTN_BACK_PIN);

    bool upTriggered     = false;
    bool downTriggered   = false;
    bool selectTriggered = false;
    bool backTriggered   = false;

    // Edge-detection + debounce: triggers once on falling edge (HIGH -> LOW)
    if ((millis() - lastDebounceTime) > BUTTON_DEBOUNCE_MS) {
        if (lastUpState == HIGH && currUp == LOW)         upTriggered = true;
        if (lastDownState == HIGH && currDown == LOW)     downTriggered = true;
        if (lastSelectState == HIGH && currSelect == LOW) selectTriggered = true;
        if (lastBackState == HIGH && currBack == LOW)     backTriggered = true;

        if (upTriggered || downTriggered || selectTriggered || backTriggered) {
            lastDebounceTime = millis();
            handleMenuInput(upTriggered, downTriggered, selectTriggered, backTriggered);
        }
    }

    lastUpState     = currUp;
    lastDownState   = currDown;
    lastSelectState = currSelect;
    lastBackState   = currBack;

    // 3. Render current OLED UI frame
    renderMenu();

    delay(15); // ~60 FPS UI loop without blocking interrupts
}
