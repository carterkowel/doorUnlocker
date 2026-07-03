// WiFi + MQTT 24-hour soak test.
// Connects to WiFi and HiveMQ, then publishes uptime + drop counts every 60s.
// Watch Serial Monitor to see reconnects in real time.
// Subscribe to door/apt1/soak/# in HiveMQ Web Client to see remote heartbeats.
//
// Flash: switch VS Code env to "test_wifi_soak", click upload.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"
#include "config.h"

static WiFiClientSecure wifiClient;
static PubSubClient     mqtt(wifiClient);

static uint32_t wifiDrops = 0;
static uint32_t mqttDrops = 0;
static bool     prevWifi  = false;
static bool     prevMqtt  = false;

void setup() {
    Serial.begin(115200);
    Serial.println("\n[SOAK] WiFi+MQTT soak test starting");
    wifiClient.setInsecure();
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
    const bool wifiUp = WiFi.status() == WL_CONNECTED;
    const bool mqttUp = mqtt.connected();

    if (wifiUp && !prevWifi) {
        Serial.printf("[SOAK] WiFi UP  ip=%s  drops=%u\n",
                      WiFi.localIP().toString().c_str(), wifiDrops);
    }
    if (!wifiUp && prevWifi) {
        wifiDrops++;
        Serial.printf("[SOAK] WiFi DROP #%u  uptime=%lus\n", wifiDrops, millis() / 1000);
    }
    prevWifi = wifiUp;

    if (wifiUp && !mqttUp) {
        mqtt.connect("door-soak", MQTT_USERNAME, MQTT_PASSWORD);
    }
    if (mqttUp && !prevMqtt) {
        Serial.printf("[SOAK] MQTT UP  drops=%u\n", mqttDrops);
        mqtt.publish("door/apt1/soak/status", "online", true);
    }
    if (!mqttUp && prevMqtt && wifiUp) {
        mqttDrops++;
        Serial.printf("[SOAK] MQTT DROP #%u\n", mqttDrops);
    }
    prevMqtt = mqttUp;

    if (mqttUp) mqtt.loop();

    static uint32_t lastPub = 0;
    if (millis() - lastPub >= 60000) {
        lastPub = millis();
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"uptime_s\":%lu,\"wifi_drops\":%u,\"mqtt_drops\":%u,\"heap\":%u}",
                 millis() / 1000, wifiDrops, mqttDrops, ESP.getFreeHeap());
        if (mqttUp) mqtt.publish("door/apt1/soak/uptime", buf);
        Serial.println(buf);
    }

    delay(100);
}
