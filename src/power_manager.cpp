#include "power_manager.h"
#include "config.h"
#include "driver/rtc_io.h"
#include <U8g2lib.h>

// Reference the global OLED display object (will be defined in main.cpp)
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

void initPowerManager() {
    // Set ADC resolution to 12-bit (0 - 4095) and 11dB attenuation (reads up to ~3.3V)
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_ADC_PIN, INPUT);

    // Log wake-up reason to Serial for debugging when connected to PC
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    if (wakeupReason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[Power] Woke up from Deep Sleep via Select Button.");
    } else {
        Serial.println("[Power] Cold boot / Reset.");
    }
}

float getBatteryVoltage() {
    // Take 16 samples and average them to filter out switching noise
    uint32_t adcSum = 0;
    for (int i = 0; i < 16; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(2);
    }
    float avgADC = adcSum / 16.0f;

    // Convert raw ADC value to pin voltage (0.0V to 3.3V)
    float pinVoltage = (avgADC / 4095.0f) * 3.3f;

    // Scale back up to actual battery voltage using config.h ratio (5.7)
    float batteryVoltage = pinVoltage * VOLTAGE_DIVIDER_RATIO;

    // Apply calibration offset defined in config.h
    // BATTERY_CALIBRATION_OFFSET : Update values on config.h from a multimeter measurement for battery voltage readings calibration
    return (batteryVoltage > 0.5f) ? (batteryVoltage + BATTERY_CALIBRATION_OFFSET) : 0.0f;
}

int getBatteryPercentage(float voltage) {
    if (voltage <= BATTERY_MIN_VOLTS) return 0;
    if (voltage >= BATTERY_MAX_VOLTS) return 100;

    float percentage = ((voltage - BATTERY_MIN_VOLTS) / (BATTERY_MAX_VOLTS - BATTERY_MIN_VOLTS)) * 100.0f;
    return (int)percentage;
}

void enterDeepSleep() {
    Serial.println("[Power] Preparing for Deep Sleep shutdown...");

    // 1. Fail-safe: Force solenoid relay LOW (closed) before sleeping
    pinMode(SOLENOID_RELAY_PIN, OUTPUT);
    digitalWrite(SOLENOID_RELAY_PIN, LOW);

    // 2. Display shutdown message briefly, then turn off OLED pixels completely
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont12_mf);
    u8g2.drawStr(20, 35, "Shutting Down...");
    u8g2.sendBuffer();
    delay(800);
    u8g2.setPowerSave(1); // Powers down OLED panel to save battery

    // 3. Configure BTN_SELECT_PIN (must be an RTC GPIO like GPIO 27) as wake-up source
    gpio_num_t wakePin = (gpio_num_t)BTN_SELECT_PIN;
    rtc_gpio_init(wakePin);
    rtc_gpio_set_direction(wakePin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(wakePin);     // Keep internal pull-up active during Deep Sleep
    rtc_gpio_pulldown_dis(wakePin);

    // Wake up when BTN_SELECT_PIN is pulled LOW (0) by pressing the button
    esp_sleep_enable_ext0_wakeup(wakePin, 0);

    Serial.println("[Power] Entering Deep Sleep now. Press SELECT to wake.");
    Serial.flush();
    esp_deep_sleep_start();
}
