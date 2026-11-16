#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================================================
// 1. FIRMWARE METADATA
// =========================================================================
#define FIRMWARE_VERSION        "v1.0.0"
#define DEVICE_BT_NAME          "WaterAudit_Rig_01"

// =========================================================================
// 2. I2C BUS & PERIPHERAL ADDRESSES (Shared by OLED & FRAM)
// =========================================================================
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22
#define I2C_CLOCK_SPEED         400000  // 400kHz Fast Mode

#define OLED_I2C_ADDRESS        0x3C
#define FRAM_I2C_ADDRESS        0x50    // MB85RC256V default address

// =========================================================================
// 3. USER INTERFACE BUTTONS (All RTC-capable GPIOs)
// =========================================================================
#define BTN_UP_PIN              25
#define BTN_DOWN_PIN            26
#define BTN_SELECT_PIN          27      // Also acts as Deep Sleep Wake-Up pin
#define BTN_BACK_PIN            32
#define BUTTON_DEBOUNCE_MS      150

// =========================================================================
// 4. SENSORS & ACTUATORS (Safe non-strapping GPIOs)
// =========================================================================
#define FLOW_SENSOR_PIN         18      // YF-S201 pulse interrupt input
#define SOLENOID_RELAY_PIN      19      // Songle 12V relay NPN transistor base
#define BUZZER_PIN              13      // Active/Passive status buzzer
#define PRESSURE_ADC_PIN        34      // ADC1_CH6 (Input only, Wi-Fi safe)

// =========================================================================
// 5. POWER & BATTERY MONITORING (ADC1)
// =========================================================================
#define BATTERY_ADC_PIN         35      // ADC1_CH7 (Input only, Wi-Fi safe)
#define VOLTAGE_DIVIDER_RATIO   5.7f    // (R1: 47k + R2: 10k) / R2: 10k
#define BATTERY_MIN_VOLTS       11.5f   // 0% threshold for 4S LiFePO4
#define BATTERY_MAX_VOLTS       13.6f   // 100% resting threshold for 4S LiFePO4
#define BATTERY_CALIBRATION_OFFSET  0.15f   // Adjust later: (Multimeter V - OLED V)

// =========================================================================
// 6. DEFAULT SENSOR CALIBRATION CONSTANTS
// =========================================================================
#define DEFAULT_FLOW_K_FACTOR   7.5f    // YF-S201 standard: ~7.5 pulses/sec per L/min (450 pulses/L)
#define DEFAULT_PRESSURE_OFFSET 0.0f    // Zero-point offset in Bar (saved to FRAM)

#endif // CONFIG_H
