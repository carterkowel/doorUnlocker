#include "AudioDetector.h"
#include "config.h"
#include "door_types.h"
#include <Arduino.h>
#include <cmath>
#include <esp_log.h>

static const char* TAG = "AudioDet";

// Holds up to 500 ms of samples at 4 kHz — prevents loss if the audio task
// is briefly preempted by a higher-priority Core 1 task.
static constexpr int SAMPLE_QUEUE_LEN = 2000;

// =============================================================================
// Timer callback  (ESP_TIMER_TASK context — regular task, not ISR)
// =============================================================================

void AudioDetector::onTimerTick(void* arg) {
    AudioDetector* ad = static_cast<AudioDetector*>(arg);
    // analogRead() is safe in task context.  Default attenuation is 11 dB
    // (0–3.3 V).  MAX4466 DC bias at ~1.65 V reads ~2048 on a 12-bit ADC.
    const int16_t sample = static_cast<int16_t>(analogRead(PIN_MIC_ADC) - 2048);
    // Non-blocking: silently drop if audio task is running behind.
    xQueueSend(ad->_sampleQueue, &sample, 0);
}

// =============================================================================
// Initialisation
// =============================================================================

void AudioDetector::begin(QueueHandle_t eventQueue) {
    _eventQueue  = eventQueue;
    _threshold   = AUDIO_THRESHOLD_DEFAULT;
    _sampleQueue = xQueueCreate(SAMPLE_QUEUE_LEN, sizeof(int16_t));
    configASSERT(_sampleQueue);

    analogReadResolution(12);

    // Pre-compute the Goertzel coefficient.
    // k = round(N * f_target / f_sample) = round(128 * 840 / 4000) = 27
    const float k     = roundf(static_cast<float>(AUDIO_BLOCK_SIZE)
                               * AUDIO_TARGET_FREQ_HZ / AUDIO_SAMPLE_RATE_HZ);
    const float omega = 2.0f * M_PI * k / static_cast<float>(AUDIO_BLOCK_SIZE);
    _coeff = 2.0f * cosf(omega);
    ESP_LOGI(TAG, "init: k=%.0f coeff=%.4f threshold=%d", k, _coeff, _threshold);

    // Start the 4 kHz hardware-backed timer (250 µs period).
    const esp_timer_create_args_t args = {
        .callback             = &AudioDetector::onTimerTick,
        .arg                  = this,
        .dispatch_method      = ESP_TIMER_TASK,
        .name                 = "audio_4k",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &_sampleTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(_sampleTimer,
                    1000000 / AUDIO_SAMPLE_RATE_HZ));
    ESP_LOGI(TAG, "4 kHz sample timer started");
}

// =============================================================================
// Per-sample processing  (called from audio FreeRTOS task in a loop)
// =============================================================================

void AudioDetector::process() {
    int16_t raw;
    // Block until a sample is available — yields Core 1 to lower-priority tasks.
    if (xQueueReceive(_sampleQueue, &raw, portMAX_DELAY) != pdTRUE) return;

    const float x = static_cast<float>(raw);
    _s2 = _s1;
    _s1 = _s0;
    _s0 = x + _coeff * _s1 - _s2;

    if (++_sampleCount >= AUDIO_BLOCK_SIZE) {
        evaluateBlock();
        _sampleCount = 0;
        _s0 = _s1 = _s2 = 0.0f;
    }
}

// =============================================================================
// Block evaluation
// =============================================================================

void AudioDetector::evaluateBlock() {
    _lastPower = _s0 * _s0 - _coeff * _s0 * _s1 + _s1 * _s1;

    if (_lastPower >= static_cast<float>(_threshold)) {
        _confirmCount++;
        if (_confirmCount >= AUDIO_CONFIRM_BLOCKS && !_buzzerActive) {
            _buzzerActive = true;
            ESP_LOGI(TAG, "buzzer confirmed! power=%.1f", _lastPower);
            postBuzzerEvent();
        }
    } else {
        _confirmCount = 0;
        _buzzerActive = false;
    }
}

void AudioDetector::setThreshold(int threshold) {
    _threshold = threshold;
    ESP_LOGI(TAG, "threshold updated to %d", threshold);
}

void AudioDetector::postBuzzerEvent() {
    DoorEvent evt{};
    evt.type = EventType::BUZZER_DETECTED;
    if (xQueueSend(_eventQueue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full — BUZZER_DETECTED dropped");
    }
}
