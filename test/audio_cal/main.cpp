// Audio threshold calibration tool.
// Prints raw Goertzel power for every 128-sample block — no threshold gate.
//
// Procedure:
//   1. Flash this, open Serial Monitor (115200).
//   2. Let it run in silence for 30s. Note the power values — this is your noise floor.
//   3. Play an 840 Hz tone from your phone held near the MAX4466.
//      (Search "840 hz tone" on YouTube, or use a tone generator app.)
//   4. Note the power values during the tone.
//   5. Set AUDIO_THRESHOLD_DEFAULT in config.h to ~60% of the way from floor to tone.
//      Example: floor=200, tone=4000 → threshold = 200 + 0.6*(4000-200) ≈ 2480 → use 2500.
//
// Flash: switch VS Code env to "test_audio_cal", click upload.

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_timer.h>
#include "config.h"

static constexpr int   SAMPLE_RATE = AUDIO_SAMPLE_RATE_HZ;
static constexpr int   BLOCK_SIZE  = AUDIO_BLOCK_SIZE;
static constexpr float TARGET_HZ   = AUDIO_TARGET_FREQ_HZ;
static constexpr int   K     = static_cast<int>(BLOCK_SIZE * TARGET_HZ / SAMPLE_RATE + 0.5f);
static constexpr float COEFF = 2.0f * cosf(2.0f * static_cast<float>(M_PI) * K / BLOCK_SIZE);

static volatile int   s_idx = 0;
static volatile float s_q1  = 0.0f;
static volatile float s_q2  = 0.0f;
static volatile bool  s_ready  = false;
static volatile float s_power  = 0.0f;

static void IRAM_ATTR onTick(void*) {
    const int raw = analogRead(PIN_MIC_ADC);
    const float s = static_cast<float>(raw) - 2048.0f;
    const float q0 = COEFF * s_q1 - s_q2 + s;
    s_q2 = s_q1;
    s_q1 = q0;
    if (++s_idx >= BLOCK_SIZE) {
        s_power = s_q1 * s_q1 + s_q2 * s_q2 - COEFF * s_q1 * s_q2;
        s_q1 = s_q2 = 0.0f;
        s_idx  = 0;
        s_ready = true;
    }
}

void setup() {
    Serial.begin(115200);
    analogSetAttenuation(ADC_11db);
    Serial.printf("\n[AUDIO CAL] k=%d  coeff=%.4f  sample_rate=%d  block=%d\n",
                  K, COEFF, SAMPLE_RATE, BLOCK_SIZE);
    Serial.println("[AUDIO CAL] Reading power every block (~32ms). Let sit silent, then play 840Hz.");
    Serial.printf("[AUDIO CAL] Current AUDIO_THRESHOLD_DEFAULT=%d\n", AUDIO_THRESHOLD_DEFAULT);

    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback        = onTick,
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "adc_cal",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&args, &timer);
    esp_timer_start_periodic(timer, 1000000 / SAMPLE_RATE);
}

void loop() {
    if (!s_ready) return;
    s_ready = false;
    Serial.printf("%.1f\n", s_power);
}
