#include "sensor_manager.h"
#include "config.h"

static volatile unsigned long pulseCount = 0;

static float currentKFactor = DEFAULT_FLOW_K_FACTOR;         // Default: 11.0 for YF-B1
static float pressureZeroOffset = DEFAULT_PRESSURE_OFFSET;  // Default: 0.0 Bar
static float currentMaxPressureBar = DEFAULT_MAX_PRESSURE_BAR; // Default: 12.0 Bar (1.2 MPa)

static unsigned long lastFlowCalcTime = 0;
static unsigned long lastPulseSnapshot = 0;
static float liveFlowRateLPM = 0.0f;
static bool solenoidIsOpen = false;

void IRAM_ATTR flowPulseISR() {
    pulseCount++;
}

void initSensorManager() {
    pinMode(SOLENOID_RELAY_PIN, OUTPUT);
    digitalWrite(SOLENOID_RELAY_PIN, LOW);
    solenoidIsOpen = false;

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(PRESSURE_ADC_PIN, INPUT);

    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulseISR, RISING);

    pinMode(CHARGER_HOLD_PIN, OUTPUT);
    digitalWrite(CHARGER_HOLD_PIN, LOW); // LOW = Charger ON by default

    lastFlowCalcTime = millis();
    Serial.println("[Sensors] Hardware initialized. Solenoid CLOSED.");
}

void updateSensors() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastFlowCalcTime;

    if (elapsed >= 500) {
        noInterrupts();
        unsigned long currentPulses = pulseCount;
        interrupts();

        unsigned long deltaPulses = currentPulses - lastPulseSnapshot;
        float frequencyHz = (deltaPulses * 1000.0f) / (float)elapsed;

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

static float readRawPressureBar() {
    uint32_t adcSum = 0;
    for (int i = 0; i < 16; i++) {
        adcSum += analogRead(PRESSURE_ADC_PIN);
        delayMicroseconds(200);
    }
    float avgADC = adcSum / 16.0f;
    float pinVoltage = (avgADC / 4095.0f) * 3.3f;

    // 0.5V-4.5V scaled via 10k/20k divider to 0.33V-3.00V across currentMaxPressureBar
    float rawBar = ((pinVoltage - 0.33f) / (3.00f - 0.33f)) * currentMaxPressureBar;
    return rawBar;
}

float readLivePressureBar() {
    float calibratedBar = readRawPressureBar() - pressureZeroOffset;
    return (calibratedBar < 0.0f) ? 0.0f : calibratedBar;
}

void zeroPressureSensor() {
    pressureZeroOffset = readRawPressureBar();
    Serial.print("[Sensors] Pressure zeroed. New offset: ");
    Serial.println(pressureZeroOffset, 3);
}

void setFlowKFactor(float kFactor) {
    if (kFactor > 0.1f) currentKFactor = kFactor;
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

void setMaxPressureBar(float maxBar) {
    if (maxBar >= 1.0f && maxBar <= 100.0f) {
        currentMaxPressureBar = maxBar;
    }
}

float getMaxPressureBar() {
    return currentMaxPressureBar;
}

void setSolenoidState(bool openValve) {
    if (openValve) {
        digitalWrite(CHARGER_HOLD_PIN, HIGH); // Pause A436 charger
        delay(20);
        digitalWrite(SOLENOID_RELAY_PIN, HIGH);     // Open 12V solenoid
    } else {
        digitalWrite(SOLENOID_RELAY_PIN, LOW);      // Close 12V solenoid
        delay(20);
        digitalWrite(CHARGER_HOLD_PIN, LOW);  // Resume A436 charger
    }

    solenoidIsOpen = openValve;
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
