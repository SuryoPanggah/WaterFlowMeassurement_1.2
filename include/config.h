#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================================================
// 1. FIRMWARE METADATA
// =========================================================================
#define FIRMWARE_VERSION            "v1.1.0"
#define DEVICE_BT_NAME              "WaterAudit_Rig_01"

// =========================================================================
// 2. 2.8" SPI TFT DISPLAY (ILI9341 240x320) & SHARED MICROSD SLOT
// =========================================================================
#define TFT_CS_PIN                  15
#define TFT_RST_PIN                 4
#define TFT_DC_PIN                  5
#define TFT_MOSI_PIN                18
#define TFT_SCK_PIN                 19
#define TFT_MISO_PIN                23
#define TFT_BL_PIN                  17      // Backlight control (HIGH = ON, LOW = Sleep)

#define SD_CS_PIN                   16      // Shared SPI bus (MOSI=18, SCK=19, MISO=23)
#define SD_SITES_CSV_PATH           "/customers/sites.csv"

// =========================================================================
// 3. I2C BUS & PERIPHERAL ADDRESSES (Shared by FRAM & Future I2C Sensors)
// =========================================================================
#define I2C_SDA_PIN                 21
#define I2C_SCL_PIN                 22
#define I2C_CLOCK_SPEED             400000  // 400kHz Fast Mode

#define FRAM_I2C_ADDRESS            0x50    // MB85RC256V default address

// =========================================================================
// 4. USER INTERFACE BUTTONS (Active-LOW with Internal Pull-Ups)
// =========================================================================
#define BTN_UP_PIN                  25
#define BTN_DOWN_PIN                26
#define BTN_SELECT_PIN              27      // RTC GPIO: Also acts as Deep Sleep Wake-Up pin
#define BTN_BACK_PIN                14
#define BUTTON_DEBOUNCE_MS          150

// =========================================================================
// 5. SENSORS & ACTUATORS (Safe Non-Strapping GPIOs)
// =========================================================================
#define FLOW_SENSOR_PIN             33      // YF-B1 pulse interrupt (via 10k/20k divider)
#define SOLENOID_RELAY_PIN          32      // Songle 12V relay NPN base (zero boot glitch)
#define BUZZER_PIN                  13      // 3.3V Active status buzzer
#define PRESSURE_ADC_PIN            34      // ADC1_CH6 (0.5-4.5V via 10k/20k divider + 0.1uF)

// =========================================================================
// 6. POWER & BATTERY MONITORING (ADC1)
// =========================================================================
#define BATTERY_ADC_PIN             35      // ADC1_CH7 (Input only, Wi-Fi safe)
#define CHARGER_HOLD_PIN            2       // GPIO 2: HIGH = Pause A436 charger during solenoid test (Avoids BTN_BACK on GPIO 14)

// --- FUTURE PRODUCTION MULTI-RAIL MONITORING (Uncomment when extra ADC1 pins are available) ---
// #define AC_SENSE_ADC_PIN     36   // GPIO 36 (VP) - Tapped BEFORE IRM-20-12 blocking diode
// #define BUS_12V_ADC_PIN      33   // GPIO 33      - Tapped AFTER ORing diodes (+12V Bus)
// #define RAIL_5V_ADC_PIN      39   // GPIO 39 (VN) - Tapped at MP1584 +5V Output
// #define DIVIDER_RATIO_5V     2.0f // 10k + 10k divider ((10 + 10) / 10 = 2.0)

#define VOLTAGE_DIVIDER_RATIO       5.7f    // (R1: 47k + R2: 10k) / R2: 10k
#define BATTERY_MIN_VOLTS           11.5f   // 0% threshold for 4S LiFePO4
#define BATTERY_MAX_VOLTS           13.6f   // 100% resting threshold for 4S LiFePO4
#define BATTERY_CALIBRATION_OFFSET  0.15f   // Adjust later: (Multimeter V - Screen V)

// =========================================================================
// 7. DEFAULT SENSOR CALIBRATION CONSTANTS (Overridden by FRAM at Boot)
// =========================================================================
#define DEFAULT_FLOW_K_FACTOR       11.0f   // YF-B1 brass sensor standard (~11.0 Hz per L/min)
#define DEFAULT_PRESSURE_OFFSET     0.0f    // Zero-point offset in Bar (saved to FRAM)
#define DEFAULT_MAX_PRESSURE_BAR    12.0f   // 1.2 MPa transducer full-scale rating (saved to FRAM)

#endif // CONFIG_H
