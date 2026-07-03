#include "LedManager.h"
#include "config.h"
#include "door_types.h"
#include <Arduino.h>

void LedManager::begin(EventGroupHandle_t sysEvents) {
    _sysEvents = sysEvents;

    pinMode(PIN_LED_WIFI,       OUTPUT);
    pinMode(PIN_LED_DOOR_STATE, OUTPUT);
    pinMode(PIN_LED_LOCK_STATE, OUTPUT);
    pinMode(PIN_LED_BUZZER,     OUTPUT);

    digitalWrite(PIN_LED_WIFI,       LOW);
    digitalWrite(PIN_LED_DOOR_STATE, LOW);
    digitalWrite(PIN_LED_LOCK_STATE, LOW);
    digitalWrite(PIN_LED_BUZZER,     LOW);
}

void LedManager::run() {
    _tick++;

    // Blink helpers (tick period = 50ms):
    //   Slow blink  : 1 Hz  — 10 ticks on / 10 ticks off
    //   Fast blink  : 5 Hz  — 2 ticks on  / 2 ticks off
    //   Pulse       : 2 Hz  — 5 ticks on  / 5 ticks off  (auto-lock countdown)
    const bool slowBlink = (_tick % 20) < 10;
    const bool fastBlink = (_tick % 4)  < 2;
    const bool pulse     = (_tick % 10) < 5;

    const EventBits_t bits = xEventGroupGetBits(_sysEvents);
    const bool wifiOk    = bits & SYS_BIT_WIFI_CONNECTED;
    const bool mqttOk    = bits & SYS_BIT_MQTT_CONNECTED;
    const bool doorOpen  = bits & SYS_BIT_DOOR_OPEN;
    const bool locked    = bits & SYS_BIT_DOOR_LOCKED;
    const bool autoLock  = bits & SYS_BIT_AUTO_LOCK_PENDING;
    const bool buzzer    = bits & SYS_BIT_BUZZER_ACTIVE;

    // WiFi/MQTT LED
    if (wifiOk && mqttOk)  digitalWrite(PIN_LED_WIFI, HIGH);
    else if (wifiOk)       digitalWrite(PIN_LED_WIFI, slowBlink ? HIGH : LOW);
    else                   digitalWrite(PIN_LED_WIFI, fastBlink ? HIGH : LOW);

    // Door state LED:  SOLID = closed,  OFF = open
    digitalWrite(PIN_LED_DOOR_STATE, doorOpen ? LOW : HIGH);

    // Lock state LED:  SOLID = locked,  PULSE = auto-lock pending,  OFF = unlocked
    if (locked)          digitalWrite(PIN_LED_LOCK_STATE, HIGH);
    else if (autoLock)   digitalWrite(PIN_LED_LOCK_STATE, pulse ? HIGH : LOW);
    else                 digitalWrite(PIN_LED_LOCK_STATE, LOW);

    // Buzzer LED: triple-blink when servo pressed, solid while tone active, off otherwise
    if (_buzzerBlink) {
        const uint32_t blinkTick = _tick - _buzzerBlinkTick;
        // 3 blinks × (2 on + 2 off) = 12 ticks = 600ms total
        if (blinkTick < 12) {
            digitalWrite(PIN_LED_BUZZER, (blinkTick % 4) < 2 ? HIGH : LOW);
        } else {
            _buzzerBlink = false;
            digitalWrite(PIN_LED_BUZZER, buzzer ? HIGH : LOW);
        }
    } else {
        digitalWrite(PIN_LED_BUZZER, buzzer ? HIGH : LOW);
    }
}

void LedManager::triggerBuzzerBlink() {
    _buzzerBlink     = true;
    _buzzerBlinkTick = _tick;
}
