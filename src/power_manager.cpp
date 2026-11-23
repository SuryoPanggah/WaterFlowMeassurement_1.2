#include "power_manager.h"
#include "config.h"
#include "driver/rtc_io.h"
#include <Adafruit_ILI9341.h>

extern Adafruit_ILI9341 tft;

void initPowerManager() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);

    // --- FUTURE PRODUCTION MULTI-RAIL PINS (Uncomment with config.h) ---
    // pinMode(AC_SENSE_ADC_PIN, INPUT);
    // pinMode(BUS_12V_ADC_PIN, INPUT);
    // pinMode(RAIL_5V_ADC_PIN, INPUT);

    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    if (wakeupReason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[Power] Woke up from Deep Sleep via Select Button.");
    } else {
        Serial.println("[Power] Cold boot / Reset.");
    }
}

float getBatteryVoltage() {
    uint32_t adcSum = 0;
    for (int i = 0; i < 16; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(2);
    }
    float avgADC = adcSum / 16.0f;
    float pinVoltage = (avgADC / 4095.0f) * 3.3f;
    float batteryVoltage = pinVoltage * VOLTAGE_DIVIDER_RATIO;
    return (batteryVoltage > 0.5f) ? (batteryVoltage + BATTERY_CALIBRATION_OFFSET) : 0.0f;
}

int getBatteryPercentage(float voltage) {
    if (voltage <= BATTERY_MIN_VOLTS) return 0;
    if (voltage >= BATTERY_MAX_VOLTS) return 100;
    return (int)(((voltage - BATTERY_MIN_VOLTS) / (BATTERY_MAX_VOLTS - BATTERY_MIN_VOLTS)) * 100.0f);
}

// --- FUTURE PRODUCTION MULTI-RAIL TELEMETRY (Uncomment when extra ADC1 pins are available) ---
/*
static float readAveragedRail(uint8_t pin, float dividerRatio) {
    uint32_t adcSum = 0;
    for (int i = 0; i < 16; i++) {
        adcSum += analogRead(pin);
        delay(2);
    }
    float avgADC = adcSum / 16.0f;
    float pinVoltage = (avgADC / 4095.0f) * 3.3f;
    float railVoltage = pinVoltage * dividerRatio;
    return (railVoltage > 0.5f) ? (railVoltage + BATTERY_CALIBRATION_OFFSET) : 0.0f;
}

float getACSupplyVoltage() {
    return readAveragedRail(AC_SENSE_ADC_PIN, VOLTAGE_DIVIDER_RATIO);
}

float getBus12VVoltage() {
    return readAveragedRail(BUS_12V_ADC_PIN, VOLTAGE_DIVIDER_RATIO);
}

float getRail5VVoltage() {
    return readAveragedRail(RAIL_5V_ADC_PIN, DIVIDER_RATIO_5V);
}

bool isACPowerActive() {
    // IRM-20-12 outputs ~12.0V before the blocking diode when 220V AC is plugged in
    return (getACSupplyVoltage() > 8.0f);
}

String getPowerSourceText() {
    return isACPowerActive() ? "AC MAINS" : "DC BATTERY";
}
*/

void enterDeepSleep() {
    Serial.println("[Power] Preparing for Deep Sleep shutdown...");

    pinMode(SOLENOID_RELAY_PIN, OUTPUT);
    digitalWrite(SOLENOID_RELAY_PIN, LOW);

    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
    tft.setTextSize(2);
    tft.setCursor(55, 110);
    tft.print("Shutting Down...");
    delay(800);

    // Put ILI9341 controller to sleep and cut backlight power
    tft.writeCommand(ILI9341_SLPIN);
    digitalWrite(TFT_BL_PIN, LOW);

    gpio_num_t wakePin = (gpio_num_t)BTN_SELECT_PIN;
    rtc_gpio_init(wakePin);
    rtc_gpio_set_direction(wakePin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(wakePin);
    rtc_gpio_pulldown_dis(wakePin);

    esp_sleep_enable_ext0_wakeup(wakePin, 0);

    Serial.println("[Power] Entering Deep Sleep now. Press SELECT to wake.");
    Serial.flush();
    esp_deep_sleep_start();
}
