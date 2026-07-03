#include "ServoController.h"
#include "config.h"
#include <Arduino.h>
#include <esp_log.h>

static const char* TAG = "Servo";

void ServoController::begin(SemaphoreHandle_t mutex) {
    _mutex = mutex;
    loadCalibrationFromNVS();

    // Drive both servos to starting positions then detach. Detaching stops the
    // PWM signal so servos hold no torque — a physical key can override the
    // deadbolt freely, and idle jitter is eliminated. Re-attach before each move.
    _deadboltServo.attach(PIN_SERVO_DEADBOLT);
    deadboltWrite(_deadboltServo, _lockDeg);
    vTaskDelay(pdMS_TO_TICKS(SERVO_DEADBOLT_HOLD_MS));
    _deadboltServo.detach();

    _buzzerServo.attach(PIN_SERVO_BUZZER);
    _buzzerServo.write(SERVO_BUZZER_IDLE_DEG);
    vTaskDelay(pdMS_TO_TICKS(500));
    _buzzerServo.detach();

    _locked = true;

    ESP_LOGI(TAG, "init: deadbolt lock=%d° unlock=%d°, buzzer press=%d°",
             _lockDeg, _unlockDeg, _buzzerDeg);
}

void ServoController::lockDeadbolt() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "mutex timeout on lock"); return;
    }
    _deadboltServo.attach(PIN_SERVO_DEADBOLT);
    deadboltWrite(_deadboltServo, _lockDeg);
    vTaskDelay(pdMS_TO_TICKS(SERVO_DEADBOLT_HOLD_MS));
    _deadboltServo.detach();
    _locked = true;
    xSemaphoreGive(_mutex);
    ESP_LOGI(TAG, "deadbolt LOCKED");
}

void ServoController::unlockDeadbolt() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "mutex timeout on unlock"); return;
    }
    _deadboltServo.attach(PIN_SERVO_DEADBOLT);
    deadboltWrite(_deadboltServo, _unlockDeg);
    vTaskDelay(pdMS_TO_TICKS(SERVO_DEADBOLT_HOLD_MS));
    _deadboltServo.detach();
    _locked = false;
    xSemaphoreGive(_mutex);
    ESP_LOGI(TAG, "deadbolt UNLOCKED");
}

void ServoController::pressBuzzer() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "mutex timeout on buzzer press"); return;
    }

    // Sweep home → press, hold, sweep press → home, then detach to stop jitter.
    _buzzerServo.attach(PIN_SERVO_BUZZER);
    sweepTo(_buzzerServo, SERVO_BUZZER_IDLE_DEG, _buzzerDeg);
    vTaskDelay(pdMS_TO_TICKS(SERVO_BUZZER_PRESS_MS));
    sweepTo(_buzzerServo, _buzzerDeg, SERVO_BUZZER_IDLE_DEG);
    _buzzerServo.detach();

    xSemaphoreGive(_mutex);
    ESP_LOGI(TAG, "buzzer press complete");
}

void ServoController::setDeadboltAngles(int lockedDeg, int unlockedDeg) {
    _lockDeg   = lockedDeg;
    _unlockDeg = unlockedDeg;
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putInt(NVS_KEY_SERVO_LOCK,   _lockDeg);
    prefs.putInt(NVS_KEY_SERVO_UNLOCK, _unlockDeg);
    prefs.end();
    ESP_LOGI(TAG, "deadbolt angles saved: lock=%d° unlock=%d°", _lockDeg, _unlockDeg);
}

void ServoController::setBuzzerPressAngle(int pressedDeg) {
    _buzzerDeg = pressedDeg;
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putInt(NVS_KEY_SERVO_BUZZER, _buzzerDeg);
    prefs.end();
    ESP_LOGI(TAG, "buzzer press angle saved: %d°", _buzzerDeg);
}

void ServoController::loadCalibrationFromNVS() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    _lockDeg   = prefs.getInt(NVS_KEY_SERVO_LOCK,   SERVO_DEADBOLT_LOCKED_DEG);
    _unlockDeg = prefs.getInt(NVS_KEY_SERVO_UNLOCK,  SERVO_DEADBOLT_UNLOCKED_DEG);
    _buzzerDeg = prefs.getInt(NVS_KEY_SERVO_BUZZER,  SERVO_BUZZER_PRESS_DEG);
    prefs.end();
    ESP_LOGI(TAG, "NVS calibration: lock=%d° unlock=%d° buzzer=%d°",
             _lockDeg, _unlockDeg, _buzzerDeg);
}
