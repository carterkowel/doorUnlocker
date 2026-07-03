#include "ReedSwitch.h"
#include "config.h"
#include "door_types.h"
#include <Arduino.h>
#include <esp_log.h>

static const char* TAG = "Reed";

void ReedSwitch::begin(QueueHandle_t eventQueue) {
    _eventQueue = eventQueue;
    pinMode(PIN_REED_SWITCH, INPUT_PULLUP);

    // Read initial state without posting an event (no "door changed" on boot).
    _lastRaw = digitalRead(PIN_REED_SWITCH);
    _doorOpen = _lastRaw;  // HIGH = open convention

    ESP_LOGI(TAG, "init: door is %s", _doorOpen ? "OPEN" : "CLOSED");
}

void ReedSwitch::run() {
    const bool raw = digitalRead(PIN_REED_SWITCH);

    if (raw != _lastRaw) {
        // Potential edge — start or restart debounce window.
        _lastRaw       = raw;
        _lastChangeMs  = millis();
        return;
    }

    // State has been stable for REED_DEBOUNCE_MS — accept it.
    if ((millis() - _lastChangeMs) >= REED_DEBOUNCE_MS && raw != _doorOpen) {
        _doorOpen = raw;

        DoorEvent evt{};
        evt.type = _doorOpen ? EventType::DOOR_OPENED : EventType::DOOR_CLOSED;
        if (xQueueSend(_eventQueue, &evt, 0) != pdTRUE) {
            ESP_LOGW(TAG, "event queue full — door event dropped");
        }

        ESP_LOGI(TAG, "door %s", _doorOpen ? "OPENED" : "CLOSED");
    }
}
