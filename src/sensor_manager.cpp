#include "sensor_manager.h"
#include "config.h"

// Volatile pulse counter modified inside hardware interrupt
static volatile unsigned long pulseCount = 0;

// Runtime calibration variables (initialized from config.h, later loaded from FRAM)
static float currentKFactor = DEFAULT_FLOW_K_FACTOR;         // Default: 7.5 pulses/sec per L/min
static float pressureZeroOffset = DEFAULT_PRESSURE_OFFSET;  // Default: 0.0 Bar

// Live flow rate tracking variables
static unsigned long lastFlowCalcTime = 0;
static unsigned long lastPulseSnapshot = 0;
static float liveFlowRateLPM = 0.0f;

// Track solenoid valve state for UI display
static bool solenoidIsOpen = false;

// High-speed Interrupt Service Routine stored in internal RAM
void IRAM_ATTR flowPulseISR() {
    pulseCount++;
}

void initSensorManager() {
    // 1. Setup Songle relay pin (Fail-safe default: LOW = Closed Valve)
    pinMode(SOLENOID_RELAY_PIN, OUTPUT);
    digitalWrite(SOLENOID_RELAY_PIN, LOW);
    solenoidIsOpen = false;

    // 2. Setup Buzzer pin
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // 3. Setup Pressure Transmitter analog input
    pinMode(PRESSURE_ADC_PIN, INPUT);

    // 4. Setup YF-S201 Flow Sensor with internal pull-up and rising-edge interrupt
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulseISR, RISING);

    lastFlowCalcTime = millis();
    Serial.println("[Sensors] Hardware initialized. Solenoid CLOSED.");
}

void updateSensors() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastFlowCalcTime;

    // Calculate instantaneous flow rate every 500ms without blocking the main loop
    if (elapsed >= 500) {
        noInterrupts();
        unsigned long currentPulses = pulseCount;
        interrupts();

        unsigned long deltaPulses = currentPulses - lastPulseSnapshot;
        float frequencyHz = (deltaPulses * 1000.0f) / (float)elapsed;

        // YF-S201 Formula: Flow Rate (L/min) = Frequency (Hz) / K-Factor
        liveFlowRateLPM = (currentKFactor > 0.0f) ? (frequencyHz / currentKFactor) : 0.0f;

        lastPulseSnapshot = currentPulses;
        lastFlowCalcTime = now;
    }
}

unsigned long getFlowPulses() {
    noInterrupts();
    unsigned long count = pulseCount;
    interrupts();
    return count;
}

float getTotalVolumeLiters() {
    // Since Frequency (Hz) = K * (L/min) = K * (L / 60s),
    // Total Pulses per Liter = K * 60 (e.g., 7.5 * 60 = 450 pulses/Liter)
    float pulsesPerLiter = currentKFactor * 60.0f;
    if (pulsesPerLiter <= 0.0f) return 0.0f;

    return (float)getFlowPulses() / pulsesPerLiter;
}

float getLiveFlowRateLPM() {
    return liveFlowRateLPM;
}

void resetFlowCounters() {
    noInterrupts();
    pulseCount = 0;
    interrupts();
    lastPulseSnapshot = 0;
    liveFlowRateLPM = 0.0f;
    lastFlowCalcTime = millis();
}

// Helper to read raw uncalibrated Bar value from 0.5V-4.5V (or 0-3.3V) transmitter
static float readRawPressureBar() {
    uint32_t adcSum = 0;
    for (int i = 0; i < 16; i++) {
        adcSum += analogRead(PRESSURE_ADC_PIN);
        delayMicroseconds(200);
    }
    float avgADC = adcSum / 16.0f;

    // Convert 12-bit ADC (0-4095) to pin voltage (0.0V - 3.3V)
    float pinVoltage = (avgADC / 4095.0f) * 3.3f;

    // Standard 0.5V - 4.5V (or scaled 0.33V - 3.0V) linear mapping
    // Note: Adjust max bar constant (e.g., 12.0 Bar) to match your transmitter's label
    float rawBar = ((pinVoltage - 0.33f) / (2.97f - 0.33f)) * 12.0f;
    return rawBar;
}

float readLivePressureBar() {
    float calibratedBar = readRawPressureBar() - pressureZeroOffset;
    // Clamp minor negative drift near 0 to clean 0.00 Bar
    return (calibratedBar < 0.0f) ? 0.0f : calibratedBar;
}

void zeroPressureSensor() {
    // Capture current atmospheric baseline so subsequent readings start at 0.00 Bar
    pressureZeroOffset = readRawPressureBar();
    Serial.print("[Sensors] Pressure zeroed. New offset: ");
    Serial.println(pressureZeroOffset, 3);
}

void setFlowKFactor(float kFactor) {
    if (kFactor > 0.1f) {
        currentKFactor = kFactor;
    }
}

float getFlowKFactor() {
    return currentKFactor;
}

void setPressureZeroOffset(float offset) {
    pressureZeroOffset = offset;
}

float getPressureZeroOffset() {
    return pressureZeroOffset;
}

void setSolenoidState(bool openValve) {
    solenoidIsOpen = openValve;
    digitalWrite(SOLENOID_RELAY_PIN, openValve ? HIGH : LOW);
    Serial.print("[Sensors] Solenoid Valve: ");
    Serial.println(openValve ? "OPEN" : "CLOSED");
}

bool getSolenoidState() {
    return solenoidIsOpen;
}

void beepBuzzer(uint16_t durationMs) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(durationMs);
    digitalWrite(BUZZER_PIN, LOW);
}
