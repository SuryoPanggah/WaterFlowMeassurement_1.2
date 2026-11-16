#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>

// Initialize GPIO pins, hardware interrupts, and default calibration constants
void initSensorManager();

// Periodic update function (call in main loop to calculate live L/min flow speed)
void updateSensors();

// --- Water Flow & Volume Functions ---
unsigned long getFlowPulses();
float getTotalVolumeLiters();
float getLiveFlowRateLPM();
void resetFlowCounters();

// --- Water Pressure Functions ---
float readLivePressureBar();
void zeroPressureSensor(); // Captures current atmospheric reading as 0.0 Bar

// --- Calibration Getters & Setters (Used by Menu & FRAM Storage) ---
void setFlowKFactor(float kFactor);
float getFlowKFactor();
void setPressureZeroOffset(float offset);
float getPressureZeroOffset();

// --- Actuator & Audio Controls ---
void setSolenoidState(bool openValve);
bool getSolenoidState();
void beepBuzzer(uint16_t durationMs = 80);

#endif // SENSOR_MANAGER_H
