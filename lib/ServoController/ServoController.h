#pragma once

#include <ESP32Servo.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// Controls the deadbolt servo and the building buzzer button servo.
// All public methods are safe to call from the StateMachine task;
// a mutex serialises concurrent servo access.
class ServoController {
public:
    ServoController() = default;

    // Call once from setup. Loads calibration from NVS, attaches servos,
    // and initialises the deadbolt to the locked position.
    void begin(SemaphoreHandle_t mutex);

    void lockDeadbolt();
    void unlockDeadbolt();
    bool isDeadboltLocked() const { return _locked; }

    // Drives the buzzer servo to the pressed position for SERVO_BUZZER_PRESS_MS,
    // then returns it to idle. Blocks the calling task for that duration.
    void pressBuzzer();

    // Calibration — values written to NVS immediately.
    void setDeadboltAngles(int lockedDeg, int unlockedDeg);
    void setBuzzerPressAngle(int pressedDeg);

private:
    void loadCalibrationFromNVS();

    // Hard clamp — protects against bad NVS values, bad MQTT commands, or
    // miscalibration that would stall the servo against a mechanical stop.
    static void deadboltWrite(Servo& s, int deg) {
        s.write(constrain(deg, SERVO_DEADBOLT_MIN_DEG, SERVO_DEADBOLT_MAX_DEG));
    }

    // Sweep servo from current position to target one degree at a time.
    // Yields to the scheduler between each step — does not starve other tasks.
    static void sweepTo(Servo& s, int fromDeg, int toDeg) {
        const int step = (toDeg > fromDeg) ? 1 : -1;
        for (int a = fromDeg; a != toDeg; a += step) {
            s.write(a);
            vTaskDelay(pdMS_TO_TICKS(SERVO_BUZZER_SWEEP_STEP_MS));
        }
        s.write(toDeg);
    }

    Servo             _deadboltServo;
    Servo             _buzzerServo;
    SemaphoreHandle_t _mutex   = nullptr;
    bool              _locked  = true;

    int _lockDeg    = 0;
    int _unlockDeg  = 90;
    int _buzzerDeg  = 60;
};
