#ifndef COMPASSHARDWARE_H
#define COMPASSHARDWARE_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>

struct TargetInstruction {
    char letter;
    float angle;
    bool useInnerRing;
    int ledIndex;
};

class CompassHardware {
public:
    CompassHardware();

    void begin();
    void update();
    void setTarget(TargetInstruction target);
    void setIdleMode(bool idle);
    void playStartupSequence();
    bool hasReachedTarget();

private:
    // --- Hardware Objects ---
    Servo servo;
    Adafruit_NeoPixel strip;

    // --- State Variables ---
    bool isMoving;
    bool insideTolerance;
    bool isIdle = false;
    unsigned long firstInsideToleranceTime;
    
    float absolutePosition;
    float targetPosition;
    float motionStartPosition;
    unsigned long motionStartTime;
    TargetInstruction currentTarget;

    // Pulse timing and feedback state
    unsigned long tHigh, tLow, tCycle;
    int dutyCycle;
    int theta;
    int thetaPrev;
    long turns;
    
    // LED Animation State
    static const int LED_COUNT = 26;
    int ledLevel[LED_COUNT];
    float lastTrailAngle;
    bool hasLastTrailAngle;

    // --- Internal Methods ---
    void moveTowardTarget();
    void stopServo();
    
    // Low-level feedback math
    bool readFeedback();
    int calculateTheta(int dc);
    void updateTurns();
    void updateAbsolutePosition();
    float normalizeAngle(float angle);
    float dramaticTargetAngle(float desiredAngle, float currentAbsolute);
    int calculateServoCommand(float error);
    float smoothStep(float x);
    float motionRampFactor();

    // LED rendering math
    void updateLights();
    void markSweepLights();
    void fadeLedTrail();
    void fadeToTargetOnly();
    void showLedLevels();
    void clearLedLevels();
    uint32_t scaledTurquoise(int level);
    float getDisplayAngle();
    int getOuterIndexFromAngle(float angle);
    int getInnerIndexFromAngle(float angle);
};

#endif