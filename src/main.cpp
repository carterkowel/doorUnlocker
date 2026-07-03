#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include "config.h"
#include "door_types.h"

#include "AudioDetector.h"
#include "ButtonHandler.h"
#include "LedManager.h"
#include "MqttManager.h"
#include "ReedSwitch.h"
#include "ServoController.h"
#include "StateMachine.h"

// =============================================================================
// Global FreeRTOS handles
// =============================================================================

static QueueHandle_t      g_eventQueue;
static EventGroupHandle_t g_sysEvents;
static SemaphoreHandle_t  g_servoMutex;

// =============================================================================
// Component instances  (globals — single owner, single device)
// =============================================================================

static ServoController g_servo;
static MqttManager     g_mqtt;
static AudioDetector   g_audio;
static StateMachine    g_stateMachine;
static LedManager      g_ledManager;
static ReedSwitch      g_reedSwitch;
static ButtonHandler   g_button;

// =============================================================================
// Task functions
// =============================================================================

// Core 0 — WiFi + MQTT management.  run() includes its own 10 ms yield.
static void taskWifiMqtt(void*) {
    for (;;) g_mqtt.run();
}

// Core 0 — LED pattern updates every 50 ms.
static void taskLedManager(void*) {
    for (;;) {
        g_ledManager.run();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Core 1 — Audio sample processing.  process() blocks on the internal sample
// queue; the esp_timer callback feeds it at 4 kHz from the esp_timer task.
static void taskAudioProc(void*) {
    for (;;) g_audio.process();
}

// Core 1 — Central door FSM.  run() blocks on the event queue.
static void taskStateMachine(void*) {
    for (;;) g_stateMachine.run();
}

// Core 1 — GPIO polling: reed switch and manual button at 50 Hz.
static void taskPeripherals(void*) {
    for (;;) {
        g_reedSwitch.run();
        g_button.run();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// =============================================================================
// Arduino setup / loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] Door Unlocker starting...");

    // --- FreeRTOS primitives ---
    g_eventQueue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(DoorEvent));
    g_sysEvents  = xEventGroupCreate();
    g_servoMutex = xSemaphoreCreateMutex();
    configASSERT(g_eventQueue && g_sysEvents && g_servoMutex);

    // --- Component initialisation (order matters: servo first to set safe position) ---
    g_servo.begin(g_servoMutex);
    g_mqtt.begin(g_eventQueue, g_sysEvents);
    g_audio.begin(g_eventQueue);
    g_stateMachine.begin(g_eventQueue, g_sysEvents, g_servo, g_mqtt, g_audio);
    g_ledManager.begin(g_sysEvents);
    g_reedSwitch.begin(g_eventQueue);
    g_button.begin(g_eventQueue);

    // --- Task creation ---
    // Core 0: WiFi/MQTT (pri 5) and LED manager (pri 2)
    xTaskCreatePinnedToCore(taskWifiMqtt,    "wifi_mqtt",  8192, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(taskLedManager,  "led_mgr",    2048, nullptr, 2, nullptr, 0);

    // Core 1: Audio processing (pri 5), State machine (pri 4), Peripheral polling (pri 3)
    // Audio uses esp_timer for sampling — its FreeRTOS task just processes the queue.
    xTaskCreatePinnedToCore(taskAudioProc,    "audio_proc",  4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(taskStateMachine, "state_mach",  8192, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(taskPeripherals,  "peripherals", 2048, nullptr, 3, nullptr, 1);

    Serial.println("[BOOT] All tasks created. Free heap: " +
                   String(esp_get_free_heap_size()) + " bytes");
}

void loop() {
    // Intentionally empty — all work runs in FreeRTOS tasks.
    // Suspending loop() prevents it from consuming idle-task CPU on Core 1.
    vTaskSuspend(nullptr);
}
