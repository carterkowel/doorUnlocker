#include "ButtonHandler.h"
#include "config.h"
#include "door_types.h"
#include <Arduino.h>
#include <esp_log.h>

static const char* TAG = "Button";

void ButtonHandler::begin(QueueHandle_t eventQueue) {
    _eventQueue = eventQueue;
    // Convention: HIGH = not pressed, LOW = pressed (button ties pin to GND).
    pinMode(PIN_BUTTON_MANUAL, INPUT_PULLUP);
    _lastRaw = digitalRead(PIN_BUTTON_MANUAL);

    ESP_LOGI(TAG, "init: button ready");
}

void ButtonHandler::run() {
    const bool raw = digitalRead(PIN_BUTTON_MANUAL);

    // Detect falling edge (HIGH → LOW = button pressed) with debounce.
    if (!raw && _lastRaw) {
        const uint32_t now = millis();
        if ((now - _lastPressMs) >= BUTTON_DEBOUNCE_MS) {
            _lastPressMs = now;

            DoorEvent evt{};
            evt.type = EventType::BUTTON_PRESSED;
            if (xQueueSend(_eventQueue, &evt, 0) != pdTRUE) {
                ESP_LOGW(TAG, "event queue full — BUTTON_PRESSED dropped");
            }

            ESP_LOGI(TAG, "manual button pressed");
        }
    }

    _lastRaw = raw;
}
