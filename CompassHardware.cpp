#include "CompassHardware.h"

// --- Tuning Constants ---
const byte SERVO_PIN = 18;
const byte FEEDBACK_PIN = 22;
const byte LED_PIN = 5;

const int OUTER_START = 0;
const int OUTER_COUNT = 18;
const int INNER_START = 18;
const int INNER_COUNT = 8;

const float LED_ANGLE_OFFSET = 0.0;
const bool LED_REVERSE = true;
const int LED_MAX_BRIGHTNESS = 255;
const int LED_FADE_AMOUNT = 12;

const int TURQ_R = 0;
const int TURQ_G = 255;
const int TURQ_B = 255;

const float MIN_DRAMATIC_TRAVEL_DEGREES = 330.0;
const int UNITS_FULL_CIRCLE = 360;
const int DUTY_SCALE = 1000;
const int DC_MIN = 29;
const int DC_MAX = 971;

const unsigned long TCYCLE_MIN = 1000;
const unsigned long TCYCLE_MAX = 1200;
const unsigned long PULSE_TIMEOUT = 3000;
const int SERVO_STOP = 1500;

const int START_OFFSET_INCREASE = 18;
const int START_OFFSET_DECREASE = 18;
const int MIN_OFFSET_INCREASE = 35;
const int MIN_OFFSET_DECREASE = 32;
const int MAX_OFFSET_INCREASE = 110;
const int MAX_OFFSET_DECREASE = 130;

const float KP_INCREASE = 4.0;
const float KP_DECREASE = 4.0;

const float RAMP_ZONE_DEGREES = 120.0;
const unsigned long ACCEL_TIME_MS = 900;
const float TARGET_TOLERANCE = 5.0;
const unsigned long SETTLE_TIME_MS = 600;

const float NEEDLE_MECHANICAL_OFFSET = 210.0;
// ------------------------

CompassHardware::CompassHardware() 
    : strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800), 
      isMoving(false), insideTolerance(false), hasLastTrailAngle(false) {}

void CompassHardware::begin() {
    pinMode(FEEDBACK_PIN, INPUT);

    strip.begin();
    strip.setBrightness(255);
    strip.clear();
    strip.show();

    clearLedLevels();

    servo.attach(SERVO_PIN, 1280, 1720);
    stopServo();

    while (!readFeedback()) {
        Serial.println("Waiting for valid feedback...");
        delay(250);
    }

    thetaPrev = theta;
    turns = 0;
    updateAbsolutePosition();
    
    Serial.print("Hardware Initialized. Absolute position = ");
    Serial.println(absolutePosition, 1);
}

void CompassHardware::update() {
    if (readFeedback()) {
        updateTurns();
        updateAbsolutePosition();

        if (isMoving) {
            moveTowardTarget();
        } else {
            stopServo();
        }
        
        updateLights();
    } else {
        stopServo();
    }
}

void CompassHardware::setTarget(TargetInstruction target) {
    isIdle = false;

    currentTarget = target;
    motionStartPosition = absolutePosition;
    motionStartTime = millis();
    
    targetPosition = dramaticTargetAngle(target.angle, absolutePosition);
    
    isMoving = true;
    insideTolerance = false;
    firstInsideToleranceTime = 0;
    hasLastTrailAngle = false;

    clearLedLevels();
    strip.clear();
    strip.show();
}

bool CompassHardware::hasReachedTarget() {
    return !isMoving;
}

void CompassHardware::moveTowardTarget() {
    float error = targetPosition - absolutePosition;

    if (abs(error) <= TARGET_TOLERANCE) {
        stopServo();

        if (!insideTolerance) {
            insideTolerance = true;
            firstInsideToleranceTime = millis();
        }

        if (millis() - firstInsideToleranceTime >= SETTLE_TIME_MS) {
            isMoving = false; 
        }
        return;
    }

    insideTolerance = false;
    firstInsideToleranceTime = 0;
    servo.writeMicroseconds(calculateServoCommand(error));
}

void CompassHardware::stopServo() {
    servo.writeMicroseconds(SERVO_STOP);
}

void CompassHardware::updateLights() {
if (isIdle) return;

    if (isMoving) {
        if (!insideTolerance) {
            markSweepLights();
            fadeLedTrail();
            showLedLevels();
        } else {
            fadeToTargetOnly();
        }
    } else {
        fadeToTargetOnly();
    }
}

// FIX: Context-Aware Ring Illumination
void CompassHardware::markSweepLights() {
    float currentAngle = getDisplayAngle();

    if (!hasLastTrailAngle) {
        lastTrailAngle = currentAngle;
        hasLastTrailAngle = true;
    }

    float delta = currentAngle - lastTrailAngle;
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;

    int steps = ceil(abs(delta) / 5.0);
    if (steps < 1) steps = 1;

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        float angle = normalizeAngle(lastTrailAngle + delta * t);

        int innerIndex = getInnerIndexFromAngle(angle);
        ledLevel[INNER_START + innerIndex] = LED_MAX_BRIGHTNESS;

        int outerIndex = getOuterIndexFromAngle(angle);
        ledLevel[OUTER_START + outerIndex] = LED_MAX_BRIGHTNESS;
    }

    lastTrailAngle = currentAngle;
}

void CompassHardware::fadeLedTrail() {
    for (int i = 0; i < LED_COUNT; i++) {
        ledLevel[i] -= LED_FADE_AMOUNT;
        if (ledLevel[i] < 0) ledLevel[i] = 0;
    }
}

void CompassHardware::fadeToTargetOnly() {
    int keepLed = currentTarget.ledIndex;

    for (int i = 0; i < LED_COUNT; i++) {
        if (i == keepLed && keepLed >= 0 && keepLed < LED_COUNT) {
            ledLevel[i] = LED_MAX_BRIGHTNESS;
        } else {
            ledLevel[i] -= LED_FADE_AMOUNT;
            if (ledLevel[i] < 0) ledLevel[i] = 0;
        }
    }
    showLedLevels();
}

void CompassHardware::showLedLevels() {
    strip.clear();
    for (int i = 0; i < LED_COUNT; i++) {
        if (ledLevel[i] > 0) {
            strip.setPixelColor(i, scaledColor(ledLevel[i]));
        }
    }
    strip.show();
}

void CompassHardware::clearLedLevels() {
    for (int i = 0; i < LED_COUNT; i++) {
        ledLevel[i] = 0;
    }
}

uint32_t CompassHardware::scaledColor(int level) {
    int r = (activeR * level) / 255;
    int g = (activeG * level) / 255;
    int b = (activeB * level) / 255;
    return strip.Color(r, g, b);
}

float CompassHardware::getDisplayAngle() {
    float displayAngle = normalizeAngle(absolutePosition + LED_ANGLE_OFFSET);
    if (LED_REVERSE) {
        displayAngle = normalizeAngle(360.0 - displayAngle);
    }
    return displayAngle;
}

int CompassHardware::getOuterIndexFromAngle(float angle) {
    int index = round(angle / 20.0);
    if (index >= OUTER_COUNT) index = 0;
    if (index < 0) index = OUTER_COUNT - 1;
    return index;
}

int CompassHardware::getInnerIndexFromAngle(float angle) {
    int index = round(angle / 45.0);
    if (index >= INNER_COUNT) index = 0;
    if (index < 0) index = INNER_COUNT - 1;
    return index;
}

bool CompassHardware::readFeedback() {
    tHigh = pulseIn(FEEDBACK_PIN, HIGH, PULSE_TIMEOUT);
    tLow  = pulseIn(FEEDBACK_PIN, LOW, PULSE_TIMEOUT);

    if (tHigh == 0 || tLow == 0) return false;

    tCycle = tHigh + tLow;
    if (tCycle < TCYCLE_MIN || tCycle > TCYCLE_MAX) return false;

    dutyCycle = (DUTY_SCALE * tHigh) / tCycle;
    theta = calculateTheta(dutyCycle);

    return true;
}

int CompassHardware::calculateTheta(int dc) {
    int result = (UNITS_FULL_CIRCLE - 1) - ((dc - DC_MIN) * UNITS_FULL_CIRCLE) / (DC_MAX - DC_MIN + 1);
    if (result < 0) result = 0;
    if (result > 359) result = 359;
    return result;
}

void CompassHardware::updateTurns() {
    if (theta < 90 && thetaPrev > 270) {
        turns++;
    } else if (theta > 270 && thetaPrev < 90) {
        turns--;
    }
    thetaPrev = theta;
}

void CompassHardware::updateAbsolutePosition() {
    absolutePosition = (turns * 360.0) + theta - NEEDLE_MECHANICAL_OFFSET;
}

float CompassHardware::normalizeAngle(float angle) {
    while (angle < 0) angle += 360.0;
    while (angle >= 360.0) angle -= 360.0;
    return angle;
}

float CompassHardware::dramaticTargetAngle(float desiredAngle, float currentAbsolute) {
    float currentDisplay = normalizeAngle(currentAbsolute);
    float positiveDelta = desiredAngle - currentDisplay;
    
    while (positiveDelta < 0.0) positiveDelta += 360.0;
    while (positiveDelta >= 360.0) positiveDelta -= 360.0;

    float negativeDelta = positiveDelta - 360.0;
    float candidates[6] = {
        positiveDelta, negativeDelta,
        positiveDelta + 360.0, negativeDelta - 360.0,
        positiveDelta + 720.0, negativeDelta - 720.0
    };

    float bestDelta = candidates[0];
    float bestDistance = 99999.0;

    for (int i = 0; i < 6; i++) {
        float d = abs(candidates[i]);
        if (d >= MIN_DRAMATIC_TRAVEL_DEGREES && d < bestDistance) {
            bestDistance = d;
            bestDelta = candidates[i];
        }
    }
    return currentAbsolute + bestDelta;
}

int CompassHardware::calculateServoCommand(float error) {
    float ramp = motionRampFactor();
    int offset;

    if (error > 0) {
        int dynamicMin = START_OFFSET_INCREASE + ((MIN_OFFSET_INCREASE - START_OFFSET_INCREASE) * ramp);
        int dynamicMax = MIN_OFFSET_INCREASE + ((MAX_OFFSET_INCREASE - MIN_OFFSET_INCREASE) * ramp);
        offset = abs(error) * KP_INCREASE;

        if (offset < dynamicMin) offset = dynamicMin;
        if (offset > dynamicMax) offset = dynamicMax;

        return SERVO_STOP + offset;
    } else {
        int dynamicMin = START_OFFSET_DECREASE + ((MIN_OFFSET_DECREASE - START_OFFSET_DECREASE) * ramp);
        int dynamicMax = MIN_OFFSET_DECREASE + ((MAX_OFFSET_DECREASE - MIN_OFFSET_DECREASE) * ramp);
        offset = abs(error) * KP_DECREASE;

        if (offset < dynamicMin) offset = dynamicMin;
        if (offset > dynamicMax) offset = dynamicMax;

        return SERVO_STOP - offset;
    }
}

float CompassHardware::smoothStep(float x) {
    if (x < 0.0) x = 0.0;
    if (x > 1.0) x = 1.0;
    return x * x * (3.0 - 2.0 * x);
}

float CompassHardware::motionRampFactor() {
    float elapsed = millis() - motionStartTime;
    float accel = smoothStep(elapsed / ACCEL_TIME_MS);
    float distanceToTarget = abs(targetPosition - absolutePosition);
    float decel = smoothStep(distanceToTarget / RAMP_ZONE_DEGREES);
    return min(accel, decel);
}

void CompassHardware::setIdleMode(bool idle) {
    isIdle = idle;

    if (isIdle) {
        clearLedLevels();
        strip.clear();
        strip.show();
    }
}

void CompassHardware:: playStartupSequence() {
    clearLedLevels();

    for (int i = LED_COUNT - 1; i>= 0; i--) {
        ledLevel[i] = LED_MAX_BRIGHTNESS;

        showLedLevels();
        delay(100);
    }

    delay(500);

    while (true) {
        bool stillFading = false;

        for (int i = 0; i < LED_COUNT; i++) {
            if (ledLevel[i] > 0) {
                ledLevel[i] -= 10;
                if (ledLevel[i] < 0) ledLevel[i] = 0;
                stillFading = true;
            }
        }

        showLedLevels();
        delay(15);

        if (!stillFading) {
            break;
        }
    }

    clearLedLevels();
    strip.clear();
    strip.show();
}