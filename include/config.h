#pragma once

// =============================================================================
// Pin Assignments
// =============================================================================
#define PIN_SERVO_DEADBOLT      18
#define PIN_SERVO_BUZZER        19
#define PIN_REED_SWITCH         4    // Internal pull-up enabled — no external resistor needed
#define PIN_BUTTON_MANUAL       5    // Internal pull-up enabled — no external resistor needed
#define PIN_MIC_ADC             36   // VP pin, ADC1_CH0 — must use ADC1 (not ADC2, shared with WiFi)
#define PIN_LED_WIFI            25
#define PIN_LED_DOOR_STATE      26
#define PIN_LED_LOCK_STATE      27
#define PIN_LED_BUZZER          14

// =============================================================================
// Servo Positions (degrees) — defaults; runtime values stored in NVS
// =============================================================================
#define SERVO_DEADBOLT_LOCKED_DEG    144
#define SERVO_DEADBOLT_UNLOCKED_DEG  38
#define SERVO_DEADBOLT_MIN_DEG       25   // hardware stop — never command below this
#define SERVO_DEADBOLT_MAX_DEG       150  // hardware stop — never command above this
#define SERVO_BUZZER_IDLE_DEG        85    // Resting position
#define SERVO_BUZZER_PRESS_DEG       40    // Button-pressed position
#define SERVO_BUZZER_MIN_DEG         25    // Hardware stop — never command below this
#define SERVO_BUZZER_MAX_DEG         100   // Hardware stop — never command above this
#define SERVO_BUZZER_PRESS_MS        5000  // Hold time at pressed position (ms)
#define SERVO_BUZZER_SWEEP_STEP_MS   5     // Delay between each 1° step during sweep (~250ms for 50° travel)
#define SERVO_DEADBOLT_HOLD_MS       600   // Time to hold deadbolt position before detaching (ms)

// =============================================================================
// Audio Detection — defaults; runtime threshold stored in NVS
// =============================================================================
// 4 kHz satisfies Nyquist for 840 Hz (limit = 2 kHz) and keeps the esp_timer
// callback period at 250 µs — safe for analogRead() in a task context.
// Increase to 8000 if you add an IRAM-safe raw ADC read in the ISR callback.
#define AUDIO_SAMPLE_RATE_HZ         4000
#define AUDIO_TARGET_FREQ_HZ         840
#define AUDIO_BLOCK_SIZE             128   // Goertzel block size (samples per evaluation)
// k = round(128 * 840 / 4000) = 27 → bin centre at 843.75 Hz (0.45% off target — fine)
#define AUDIO_THRESHOLD_DEFAULT      20000000  // Tune during commissioning (see AudioDetector.h)
#define AUDIO_CONFIRM_BLOCKS         8         // Consecutive above-threshold blocks = confirmed (~256ms)

// =============================================================================
// Timing
// =============================================================================
#define AUTO_LOCK_DELAY_S_DEFAULT    60
#define WIFI_RECONNECT_MS            5000
#define MQTT_RECONNECT_MS            3000
#define BUZZER_NOTIFY_COOLDOWN_MS    10000  // Minimum ms between buzzer ntfy notifications
#define BUTTON_DEBOUNCE_MS           50
#define REED_DEBOUNCE_MS             100
#define RSSI_PUBLISH_INTERVAL_MS     60000
#define HEAP_CHECK_INTERVAL_MS       30000
#define HEAP_CRITICAL_BYTES          10240  // Controlled restart if heap falls below this

// =============================================================================
// FreeRTOS
// =============================================================================
#define EVENT_QUEUE_DEPTH            20
#define PUBLISH_QUEUE_DEPTH          10

// =============================================================================
// MQTT Topics
// =============================================================================
#define MQTT_TOPIC_BASE              "door/apt1"
#define MQTT_TOPIC_CMD_DEADBOLT      MQTT_TOPIC_BASE "/cmd/deadbolt"
#define MQTT_TOPIC_CMD_BUZZER        MQTT_TOPIC_BASE "/cmd/buzzer"
#define MQTT_TOPIC_CMD_QUERY         MQTT_TOPIC_BASE "/cmd/query"
#define MQTT_TOPIC_CMD_CONFIG        MQTT_TOPIC_BASE "/cmd/config"
#define MQTT_TOPIC_STATUS_LOCK       MQTT_TOPIC_BASE "/status/lock"
#define MQTT_TOPIC_STATUS_DOOR       MQTT_TOPIC_BASE "/status/door"
#define MQTT_TOPIC_STATUS_ONLINE     MQTT_TOPIC_BASE "/status/online"
#define MQTT_TOPIC_STATUS_RSSI       MQTT_TOPIC_BASE "/status/wifi_rssi"

// =============================================================================
// NVS
// =============================================================================
#define NVS_NAMESPACE                "door_cfg"
#define NVS_KEY_SERVO_LOCK           "srv_lock"
#define NVS_KEY_SERVO_UNLOCK         "srv_unlk"
#define NVS_KEY_SERVO_BUZZER         "srv_buzz"
#define NVS_KEY_AUDIO_THRESHOLD      "aud_thr"
#define NVS_KEY_AUTO_LOCK_DELAY      "al_delay"
