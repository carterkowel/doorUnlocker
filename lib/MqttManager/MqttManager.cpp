#include "MqttManager.h"
#include "door_types.h"
#include "config.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <Arduino.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <algorithm>

static const char* TAG = "MQTT";

// Static instance pointer required for PubSubClient's C-style callback.
static MqttManager* s_instance = nullptr;

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
    if (s_instance) s_instance->handleIncomingMessage(topic, payload, len);
}

// =============================================================================
// Construction
// =============================================================================

MqttManager::MqttManager() : _mqttClient(_wifiClient) {}

// =============================================================================
// Lifecycle
// =============================================================================

void MqttManager::begin(QueueHandle_t eventQueue, EventGroupHandle_t sysEvents) {
    s_instance    = this;
    _eventQueue   = eventQueue;
    _sysEvents    = sysEvents;

    _publishQueue = xQueueCreate(PUBLISH_QUEUE_DEPTH, sizeof(PublishMessage));
    configASSERT(_publishQueue);

    // Skip certificate verification — acceptable for a personal home project.
    // For production, replace with wifiClient.setCACert(letsencryptRootCA).
    _wifiClient.setInsecure();

    _mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    _mqttClient.setBufferSize(512);
    _mqttClient.setCallback(mqttCallback);

    WiFi.mode(WIFI_STA);
    connectWifi();
}

void MqttManager::run() {
    const uint32_t now = millis();

    // Read current state before any modifications to detect transitions.
    const EventBits_t prevBits = xEventGroupGetBits(_sysEvents);
    const bool prevWifiUp = (prevBits & SYS_BIT_WIFI_CONNECTED) != 0;
    const bool prevMqttUp = (prevBits & SYS_BIT_MQTT_CONNECTED) != 0;

    const bool wifiUp = (WiFi.status() == WL_CONNECTED);
    const bool mqttUp = _mqttClient.connected();

    // --- Update event group bits to reflect current state ---
    if (wifiUp) xEventGroupSetBits(_sysEvents, SYS_BIT_WIFI_CONNECTED);
    else        xEventGroupClearBits(_sysEvents, SYS_BIT_WIFI_CONNECTED | SYS_BIT_MQTT_CONNECTED);
    if (mqttUp) xEventGroupSetBits(_sysEvents, SYS_BIT_MQTT_CONNECTED);
    else        xEventGroupClearBits(_sysEvents, SYS_BIT_MQTT_CONNECTED);

    // --- Post transition events to the door event queue ---
    if (wifiUp && !prevWifiUp) {
        _wifiRetryDelayMs = WIFI_RECONNECT_MS;
        postEvent(EventType::WIFI_CONNECTED);
        ESP_LOGI(TAG, "WiFi connected: %s", WiFi.localIP().toString().c_str());
    }
    if (!wifiUp && prevWifiUp) {
        postEvent(EventType::WIFI_DISCONNECTED);
        // MQTT is implicitly down when WiFi drops; post if it was up.
        if (prevMqttUp) postEvent(EventType::MQTT_CONN_DOWN);
        ESP_LOGW(TAG, "WiFi disconnected");
    }
    if (mqttUp && !prevMqttUp && wifiUp) {
        _mqttRetryDelayMs = MQTT_RECONNECT_MS;
        postEvent(EventType::MQTT_CONN_UP);
        ESP_LOGI(TAG, "MQTT connected");
    }
    if (!mqttUp && prevMqttUp && wifiUp) {
        // MQTT dropped independently (broker restart, network glitch, etc.)
        postEvent(EventType::MQTT_CONN_DOWN);
        ESP_LOGW(TAG, "MQTT disconnected");
    }

    // --- WiFi reconnection ---
    if (!wifiUp && (now - _lastWifiRetryMs >= _wifiRetryDelayMs)) {
        _lastWifiRetryMs  = now;
        _wifiRetryDelayMs = std::min(_wifiRetryDelayMs * 2, 60000U);
        ESP_LOGW(TAG, "WiFi reconnecting (backoff=%us)...", _wifiRetryDelayMs / 1000);
        connectWifi();
    }

    // --- MQTT reconnection ---
    if (wifiUp && !mqttUp && (now - _lastMqttRetryMs >= _mqttRetryDelayMs)) {
        _lastMqttRetryMs = now;
        if (!connectMqtt()) {
            _mqttRetryDelayMs = std::min(_mqttRetryDelayMs * 2, 60000U);
        }
    }

    // --- Service active MQTT connection ---
    if (mqttUp) {
        _mqttClient.loop();   // process incoming messages (triggers mqttCallback)
        drainPublishQueue();  // send anything the StateMachine enqueued
    }

    // --- Periodic diagnostics ---
    if (now - _lastRssiPublishMs >= RSSI_PUBLISH_INTERVAL_MS) {
        _lastRssiPublishMs = now;
        publishRssiInternal();
    }
    if (now - _lastHeapCheckMs >= HEAP_CHECK_INTERVAL_MS) {
        _lastHeapCheckMs = now;
        checkHeap();
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz yield; keeps loop() from starving other Core 0 tasks
}

// =============================================================================
// Connection helpers
// =============================================================================

bool MqttManager::connectWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    return true;
}

bool MqttManager::connectMqtt() {
    ESP_LOGI(TAG, "MQTT connecting to %s:%d", MQTT_HOST, MQTT_PORT);

    const bool ok = _mqttClient.connect(
        MQTT_CLIENT_ID,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_TOPIC_STATUS_ONLINE,  // LWT topic
        1,                         // LWT QoS
        true,                      // LWT retain
        "offline"                  // LWT payload
    );

    if (ok) {
        subscribeTopics();
        // Publish online immediately after connecting.
        _mqttClient.publish(MQTT_TOPIC_STATUS_ONLINE, "online", true);
    } else {
        ESP_LOGW(TAG, "MQTT connect failed: state=%d", _mqttClient.state());
    }

    return ok;
}

void MqttManager::subscribeTopics() {
    _mqttClient.subscribe(MQTT_TOPIC_CMD_DEADBOLT, 1);
    _mqttClient.subscribe(MQTT_TOPIC_CMD_BUZZER,   1);
    _mqttClient.subscribe(MQTT_TOPIC_CMD_QUERY,    1);
    _mqttClient.subscribe(MQTT_TOPIC_CMD_CONFIG,   1);
    ESP_LOGI(TAG, "subscribed to command topics");
}

// =============================================================================
// Incoming message parsing
// =============================================================================

void MqttManager::handleIncomingMessage(char* topic, byte* rawPayload, unsigned int len) {
    // Null-terminate — payload is not guaranteed to be null-terminated.
    char payload[128];
    const size_t copyLen = std::min(len, sizeof(payload) - 1);
    memcpy(payload, rawPayload, copyLen);
    payload[copyLen] = '\0';

    ESP_LOGD(TAG, "rx: %s = %s", topic, payload);

    if (strcmp(topic, MQTT_TOPIC_CMD_DEADBOLT) == 0) {
        DoorEvent evt{};
        if (strcmp(payload, "LOCK") == 0)        evt.type = EventType::CMD_LOCK;
        else if (strcmp(payload, "UNLOCK") == 0) evt.type = EventType::CMD_UNLOCK;
        else { ESP_LOGW(TAG, "unknown deadbolt cmd: %s", payload); return; }
        xQueueSend(_eventQueue, &evt, 0);

    } else if (strcmp(topic, MQTT_TOPIC_CMD_BUZZER) == 0) {
        if (strcmp(payload, "PRESS") == 0) {
            DoorEvent evt{};
            evt.type = EventType::CMD_BUZZER_PRESS;
            xQueueSend(_eventQueue, &evt, 0);
        }

    } else if (strcmp(topic, MQTT_TOPIC_CMD_QUERY) == 0) {
        DoorEvent evt{};
        evt.type = EventType::CMD_QUERY_STATUS;
        xQueueSend(_eventQueue, &evt, 0);

    } else if (strcmp(topic, MQTT_TOPIC_CMD_CONFIG) == 0) {
        // Expected payload: {"key":"audio_threshold","value":1800}
        // Minimal parse — no ArduinoJson dependency.
        const char* keyStart = strstr(payload, "\"key\":\"");
        const char* valStart = strstr(payload, "\"value\":");
        if (!keyStart || !valStart) { ESP_LOGW(TAG, "malformed config payload"); return; }

        char keyName[32];
        sscanf(keyStart + 7, "%31[^\"]", keyName);
        const int32_t value = static_cast<int32_t>(atoi(valStart + 8));

        DoorEvent evt{};
        evt.type        = EventType::CMD_SET_CONFIG;
        evt.configValue = value;

        if      (strcmp(keyName, "servo_lock_deg")   == 0) evt.configKey = ConfigKey::SERVO_LOCK_DEG;
        else if (strcmp(keyName, "servo_unlock_deg") == 0) evt.configKey = ConfigKey::SERVO_UNLOCK_DEG;
        else if (strcmp(keyName, "servo_buzzer_deg") == 0) evt.configKey = ConfigKey::SERVO_BUZZER_DEG;
        else if (strcmp(keyName, "audio_threshold")  == 0) evt.configKey = ConfigKey::AUDIO_THRESHOLD;
        else if (strcmp(keyName, "auto_lock_delay_s")== 0) evt.configKey = ConfigKey::AUTO_LOCK_DELAY_S;
        else { ESP_LOGW(TAG, "unknown config key: %s", keyName); return; }

        xQueueSend(_eventQueue, &evt, 0);
        ESP_LOGI(TAG, "config cmd: %s = %d", keyName, static_cast<int>(value));
    }
}

// =============================================================================
// Outbound publish queue
// =============================================================================

void MqttManager::enqueuePublish(const char* topic, const char* payload,
                                  uint8_t qos, bool retain) {
    PublishMessage msg{};
    strncpy(msg.topic,   topic,   sizeof(msg.topic)   - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.qos    = qos;
    msg.retain = retain;

    if (xQueueSend(_publishQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "publish queue full — dropped: %s", topic);
    }
}

void MqttManager::drainPublishQueue() {
    PublishMessage msg;
    while (xQueueReceive(_publishQueue, &msg, 0) == pdTRUE) {
        if (!_mqttClient.publish(msg.topic, msg.payload, msg.retain)) {
            ESP_LOGW(TAG, "publish failed: %s", msg.topic);
        }
    }
}

// =============================================================================
// Convenience status publishers
// =============================================================================

void MqttManager::publishLockStatus(bool locked) {
    enqueuePublish(MQTT_TOPIC_STATUS_LOCK, locked ? "LOCKED" : "UNLOCKED", 1, true);
}

void MqttManager::publishDoorStatus(bool open) {
    enqueuePublish(MQTT_TOPIC_STATUS_DOOR, open ? "OPEN" : "CLOSED", 1, true);
}

void MqttManager::publishAllStatus(bool locked, bool doorOpen) {
    publishLockStatus(locked);
    publishDoorStatus(doorOpen);
    enqueuePublish(MQTT_TOPIC_STATUS_ONLINE, "online", 1, true);
}

// =============================================================================
// Diagnostics
// =============================================================================

void MqttManager::publishRssiInternal() {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
    enqueuePublish(MQTT_TOPIC_STATUS_RSSI, buf, 0, false);
}

void MqttManager::checkHeap() {
    const size_t freeHeap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "free heap: %u bytes", static_cast<unsigned>(freeHeap));
    if (freeHeap < HEAP_CRITICAL_BYTES) {
        ESP_LOGE(TAG, "heap critically low (%u bytes) — restarting", static_cast<unsigned>(freeHeap));
        esp_restart();
    }
}

// =============================================================================
// Push notifications (ntfy.sh)
// =============================================================================

void MqttManager::ntfyPost(const char* title, const char* message, const char* priority) {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    char url[128];
    snprintf(url, sizeof(url), "%s/%s", NTFY_HOST, NTFY_TOPIC);

    http.begin(url);
    http.addHeader("Title",    title);
    http.addHeader("Priority", priority);
    http.addHeader("Tags",     "lock");

    const int code = http.POST(message);
    if (code != 200) {
        ESP_LOGW(TAG, "ntfy POST failed: http=%d", code);
    }
    http.end();
}

// =============================================================================
// Internal helpers
// =============================================================================

void MqttManager::postEvent(EventType type) {
    DoorEvent evt{};
    evt.type = type;
    xQueueSend(_eventQueue, &evt, 0);
}
