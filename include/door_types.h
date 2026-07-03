#pragma once

#include <stdint.h>

// =============================================================================
// Event Group Bits  (read by LedManager; set/cleared by StateMachine/MqttManager)
// =============================================================================
#define SYS_BIT_WIFI_CONNECTED     (1U << 0)
#define SYS_BIT_MQTT_CONNECTED     (1U << 1)
#define SYS_BIT_DOOR_OPEN          (1U << 2)
#define SYS_BIT_DOOR_LOCKED        (1U << 3)
#define SYS_BIT_BUZZER_ACTIVE      (1U << 4)
#define SYS_BIT_AUTO_LOCK_PENDING  (1U << 5)

// =============================================================================
// Door States
// =============================================================================
enum class DoorState : uint8_t {
    INITIALIZING = 0,
    LOCKED,
    UNLOCKED,
    DOOR_OPEN,
    AUTO_LOCK_PENDING,
    ERROR,
};

// =============================================================================
// Event Types  (posted to the central event queue by all tasks)
// =============================================================================
enum class EventType : uint8_t {
    CMD_LOCK = 0,
    CMD_UNLOCK,
    CMD_BUZZER_PRESS,
    CMD_QUERY_STATUS,
    CMD_SET_CONFIG,
    DOOR_OPENED,
    DOOR_CLOSED,
    BUZZER_DETECTED,
    BUTTON_PRESSED,
    WIFI_CONNECTED,
    WIFI_DISCONNECTED,
    MQTT_CONN_UP,
    MQTT_CONN_DOWN,
    AUTO_LOCK_TIMER,
};

// =============================================================================
// Config Keys  (used with CMD_SET_CONFIG events)
// =============================================================================
enum class ConfigKey : uint8_t {
    SERVO_LOCK_DEG = 0,
    SERVO_UNLOCK_DEG,
    SERVO_BUZZER_DEG,
    AUDIO_THRESHOLD,
    AUTO_LOCK_DELAY_S,
};

// =============================================================================
// Door Event  (fixed-size struct posted to the central event queue)
// =============================================================================
struct DoorEvent {
    EventType type        = EventType::CMD_QUERY_STATUS;
    ConfigKey configKey   = ConfigKey::AUDIO_THRESHOLD;  // valid when type == CMD_SET_CONFIG
    int32_t   configValue = 0;                            // valid when type == CMD_SET_CONFIG
};
