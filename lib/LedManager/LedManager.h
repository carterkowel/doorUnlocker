#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// Drives four status LEDs based on system event group bits.
// All patterns are non-blocking (no delay()) — driven by a 50ms tick counter.
//
// LED patterns:
//   WiFi/MQTT : SOLID=WiFi+MQTT  SLOW_BLINK=WiFi only  FAST_BLINK=no WiFi
//   Lock      : SOLID=LOCKED  PULSE=auto-lock pending  OFF=UNLOCKED
//   Door      : SOLID=CLOSED  OFF=OPEN
//   Buzzer    : ON for 800ms when tone detected; fast triple-blink when servo pressed
class LedManager {
public:
    LedManager() = default;

    void begin(EventGroupHandle_t sysEvents);

    // Update all LED outputs. Call every 50ms from the LED task.
    void run();

    // Called by StateMachine when the buzzer servo is pressed (triggers 3-blink pattern).
    void triggerBuzzerBlink();

private:
    EventGroupHandle_t _sysEvents  = nullptr;
    uint32_t           _tick       = 0;

    // Buzzer triple-blink state
    bool     _buzzerBlink     = false;
    uint32_t _buzzerBlinkTick = 0;
};
