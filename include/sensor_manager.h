#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>

void initSensorManager();
void updateSensors();

// --- Water Flow & Volume Functions ---
unsigned long getFlowPulses();
float getTotalVolumeLiters();
float getLiveFlowRateLPM();
void resetFlowCounters();

// --- Water Pressure Functions ---
float readLivePressureBar();
void zeroPressureSensor();

// --- Calibration Getters & Setters (Used by Menu, Web Portal & FRAM) ---
void setFlowKFactor(float kFactor);
float getFlowKFactor();
void setPressureZeroOffset(float offset);
float getPressureZeroOffset();
void setMaxPressureBar(float maxBar);
float getMaxPressureBar();

// --- Actuator & Audio Controls ---
void setSolenoidState(bool openValve);
bool getSolenoidState();
void beepBuzzer(uint16_t durationMs = 80);

#endif // SENSOR_MANAGER_H
