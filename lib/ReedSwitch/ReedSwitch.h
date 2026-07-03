#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Monitors the door reed switch (GPIO 34, input-only).
// Requires an external 10kΩ pull-up to 3.3V.
// Convention: switch OPEN (HIGH) = door OPEN; switch CLOSED (LOW) = door CLOSED.
//
// Posts DOOR_OPENED and DOOR_CLOSED events with REED_DEBOUNCE_MS debouncing.
class ReedSwitch {
public:
    ReedSwitch() = default;

    void begin(QueueHandle_t eventQueue);

    // Poll the GPIO and post events on state change. Call at ~10–50 Hz.
    void run();

    bool isDoorOpen() const { return _doorOpen; }

private:
    QueueHandle_t _eventQueue    = nullptr;
    bool          _doorOpen      = false;
    bool          _lastRaw       = false;
    uint32_t      _lastChangeMs  = 0;
};
