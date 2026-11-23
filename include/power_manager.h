#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

// Initialize ADC resolution and check why the ESP32 booted (Power-on vs Button Wake)
void initPowerManager();

// Read averaged battery voltage through the 47k/10k voltage divider
float getBatteryVoltage();

// Convert battery voltage into a 0-100% charge estimate for 4S LiFePO4
int getBatteryPercentage(float voltage);

// Safely close the solenoid, turn off the OLED, and enter Deep Sleep until BTN_SELECT is pressed
void enterDeepSleep();

// --- FUTURE PRODUCTION MULTI-RAIL MONITORING (Uncomment when extra ADC1 pins are available) ---
// float getACSupplyVoltage();
// float getBus12VVoltage();
// float getRail5VVoltage();
// bool isACPowerActive();
// String getPowerSourceText();

#endif // POWER_MANAGER_H
