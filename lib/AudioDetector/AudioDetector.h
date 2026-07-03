#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_timer.h>

// Detects the building buzzer frequency (840 Hz) using the Goertzel algorithm.
//
// Timing architecture (why this matters):
//   Sampling must occur at a precise 8 kHz rate. FreeRTOS at 1000 Hz tick
//   cannot achieve 125 µs inter-sample intervals. Instead, an esp_timer fires
//   at 8 kHz, reads the ADC, and posts each raw value to an internal queue.
//   The FreeRTOS audio task calls process() in a loop, which blocks on the
//   queue — allowing the StateMachine and Button tasks to run when no sample
//   is waiting.
//
// Commissioning:
//   1. Play the building buzzer. Watch serial for "last_power" values.
//   2. Set AUDIO_THRESHOLD_DEFAULT in config.h between the ambient noise floor
//      and the peak buzzer power. Save via MQTT cmd/config at runtime.
class AudioDetector {
public:
    AudioDetector() = default;

    // Call once from setup before starting the audio task.
    void begin(QueueHandle_t eventQueue);

    // Block on the internal sample queue, process one sample through Goertzel,
    // and post BUZZER_DETECTED to eventQueue when confirmed.
    // Call in a tight loop from the audio FreeRTOS task.
    void process();

    // Runtime threshold update.
    void setThreshold(int threshold);

    int   getThreshold() const { return _threshold; }
    float getLastPower()  const { return _lastPower; }

private:
    static void onTimerTick(void* arg);
    void evaluateBlock();
    void postBuzzerEvent();

    QueueHandle_t      _eventQueue   = nullptr;
    QueueHandle_t      _sampleQueue  = nullptr;  // int16_t ADC samples from timer ISR
    esp_timer_handle_t _sampleTimer  = nullptr;
    int                _threshold    = 1500;
    float              _coeff        = 0.0f;

    // Goertzel running state
    float _s0 = 0.0f, _s1 = 0.0f, _s2 = 0.0f;
    int   _sampleCount  = 0;
    int   _confirmCount = 0;
    float _lastPower    = 0.0f;
    bool  _buzzerActive = false;
};
