#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Monitors the manual unlock button (GPIO 35, input-only).
// Requires an external 10kΩ pull-up to 3.3V.
// Convention: HIGH = not pressed; LOW = pressed (button connects pin to GND).
//
// Posts BUTTON_PRESSED on the falling edge with BUTTON_DEBOUNCE_MS debouncing.
class ButtonHandler {
public:
    ButtonHandler() = default;

    void begin(QueueHandle_t eventQueue);

    // Poll the GPIO. Call at ~10–50 Hz.
    void run();

private:
    QueueHandle_t _eventQueue   = nullptr;
    bool          _lastRaw      = true;   // HIGH = not pressed
    uint32_t      _lastPressMs  = 0;
};
