#pragma once

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include "door_types.h"
#include "config.h"

// Manages WiFi connection, MQTT (HiveMQ Cloud TLS via PubSubClient), and
// outbound push notifications via ntfy.sh.
//
// Thread model:
//   run() must be called exclusively from the WiFi/MQTT task (Core 0).
//   enqueuePublish() is safe to call from any task.
//   PubSubClient and WiFiClientSecure are NOT thread-safe — all socket calls
//   happen inside run() on the WiFi/MQTT task only.
class MqttManager {
public:
    MqttManager();

    // eventQueue: receives WIFI_CONNECTED/DISCONNECTED, MQTT_CONNECTED/DISCONNECTED.
    // sysEvents:  SYS_BIT_WIFI_CONNECTED and SYS_BIT_MQTT_CONNECTED managed here.
    void begin(QueueHandle_t eventQueue, EventGroupHandle_t sysEvents);

    // Main loop body for the WiFi/MQTT task. Handles reconnection backoff,
    // calls mqttClient.loop(), drains the publish queue, and publishes
    // periodic RSSI/heap diagnostics. Yields via vTaskDelay at the end.
    void run();

    // Enqueue an MQTT publish for the WiFi/MQTT task to send. Safe from any task.
    void enqueuePublish(const char* topic, const char* payload,
                        uint8_t qos = 1, bool retain = false);

    // Convenience wrappers — all call enqueuePublish internally.
    void publishLockStatus(bool locked);
    void publishDoorStatus(bool open);
    void publishAllStatus(bool locked, bool doorOpen);

    bool isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }
    bool isMqttConnected() { return _mqttClient.connected(); }

    // HTTP POST to ntfy.sh. Call from StateMachine task for push notifications.
    // Silently no-ops if WiFi is not connected.
    static void ntfyPost(const char* title, const char* message,
                         const char* priority = "default");

    // Called by the static PubSubClient callback to route incoming messages.
    void handleIncomingMessage(char* topic, byte* payload, unsigned int len);

private:
    struct PublishMessage {
        char    topic[64];
        char    payload[64];
        uint8_t qos;
        bool    retain;
    };

    bool connectWifi();
    bool connectMqtt();
    void subscribeTopics();
    void drainPublishQueue();
    void postEvent(EventType type);
    void publishRssiInternal();
    void checkHeap();

    WiFiClientSecure   _wifiClient;
    PubSubClient       _mqttClient;
    QueueHandle_t      _eventQueue    = nullptr;
    QueueHandle_t      _publishQueue  = nullptr;
    EventGroupHandle_t _sysEvents     = nullptr;

    uint32_t _lastWifiRetryMs   = 0;
    uint32_t _lastMqttRetryMs   = 0;
    uint32_t _wifiRetryDelayMs  = WIFI_RECONNECT_MS;
    uint32_t _mqttRetryDelayMs  = MQTT_RECONNECT_MS;
    uint32_t _lastRssiPublishMs = 0;
    uint32_t _lastHeapCheckMs   = 0;
};
