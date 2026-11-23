#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include "config.h"
#include "power_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "menu_manager.h"

// Global 2.8" 320x240 SPI TFT instance using shared hardware/custom SPI bus
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC_PIN, TFT_CS_PIN, TFT_RST_PIN);

static bool lastUpState = HIGH;
static bool lastDownState = HIGH;
static bool lastSelectState = HIGH;
static bool lastBackState = HIGH;
static unsigned long lastDebounceTime = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Water Audit Rig Booting ===");

    // 1. Initialize I2C bus for MB85RC256V FRAM
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_CLOCK_SPEED);

    // 2. Configure 4 physical UI buttons
    pinMode(BTN_UP_PIN, INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_SELECT_PIN, INPUT_PULLUP);
    pinMode(BTN_BACK_PIN, INPUT_PULLUP);

    // 3. Deselect both SPI Chip Selects before initializing SPI bus
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    SPI.begin(TFT_SCK_PIN, TFT_MISO_PIN, TFT_MOSI_PIN);

    // 4. Initialize 2.8" ILI9341 TFT in Landscape (320x240)
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(0x0841); // Deep slate background
    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(3);
    tft.setCursor(28, 85);
    tft.print("WATER AUDIT RIG");
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setTextSize(2);
    tft.setCursor(120, 125);
    tft.print(FIRMWARE_VERSION);

    // 5. Initialize Power, Sensors, FRAM & microSD
    initPowerManager();
    initSensorManager();

    if (initStorageManager()) {
        float savedK = DEFAULT_FLOW_K_FACTOR;
        float savedOffset = DEFAULT_PRESSURE_OFFSET;
        float savedMaxBar = DEFAULT_MAX_PRESSURE_BAR;
        loadCalibrationFromFRAM(savedK, savedOffset, savedMaxBar);
        setFlowKFactor(savedK);
        setPressureZeroOffset(savedOffset);
        setMaxPressureBar(savedMaxBar);
    }

    // 6. Initialize Network & TFT Menu Engine
    initNetworkManager();
    initMenuManager();

    delay(800);
    beepBuzzer(60);
    Serial.println("=== System Ready ===");
}

void loop() {
    updateSensors();
    updateNetwork();

    bool currUp     = digitalRead(BTN_UP_PIN);
    bool currDown   = digitalRead(BTN_DOWN_PIN);
    bool currSelect = digitalRead(BTN_SELECT_PIN);
    bool currBack   = digitalRead(BTN_BACK_PIN);

    bool upTriggered     = false;
    bool downTriggered   = false;
    bool selectTriggered = false;
    bool backTriggered   = false;

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

    renderMenu();
    delay(15);
}
