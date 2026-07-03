#include "StateMachine.h"
#include "config.h"
#include "door_types.h"
#include "ServoController.h"
#include "MqttManager.h"
#include "AudioDetector.h"
#include <Preferences.h>
#include <Arduino.h>
#include <esp_log.h>

static const char* TAG = "StateMachine";

// =============================================================================
// Lifecycle
// =============================================================================

void StateMachine::begin(QueueHandle_t eventQueue,
                          EventGroupHandle_t sysEvents,
                          ServoController& servo,
                          MqttManager& mqtt,
                          AudioDetector& audio) {
    _eventQueue = eventQueue;
    _sysEvents  = sysEvents;
    _servo      = &servo;
    _mqtt       = &mqtt;
    _audio      = &audio;

    // Load runtime-configurable auto-lock delay from NVS.
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    _autoLockDelayS = prefs.getInt(NVS_KEY_AUTO_LOCK_DELAY, AUTO_LOCK_DELAY_S_DEFAULT);
    prefs.end();

    _autoLockTimer = xTimerCreate(
        "auto_lock",
        pdMS_TO_TICKS(static_cast<uint32_t>(_autoLockDelayS) * 1000U),
        pdFALSE,    // one-shot
        this,
        autoLockTimerCallback
    );
    configASSERT(_autoLockTimer);

    _buzzerLedTimer = xTimerCreate(
        "buzz_led",
        pdMS_TO_TICKS(3000),  // clear buzzer LED after 3s
        pdFALSE,
        this,
        buzzerLedTimerCallback
    );
    configASSERT(_buzzerLedTimer);

    // Reflect initial locked state in event group.
    xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
    xEventGroupClearBits(_sysEvents, SYS_BIT_DOOR_OPEN | SYS_BIT_AUTO_LOCK_PENDING | SYS_BIT_BUZZER_ACTIVE);

    _state = DoorState::INITIALIZING;
    ESP_LOGI(TAG, "init complete, auto-lock delay=%ds", static_cast<int>(_autoLockDelayS));
}

void StateMachine::run() {
    DoorEvent evt{};
    // Block indefinitely — all work is event-driven.
    if (xQueueReceive(_eventQueue, &evt, portMAX_DELAY) != pdTRUE) return;

    DoorState next = _state;
    bool consumed = handleCommon(evt, next);

    if (!consumed) {
        switch (_state) {
            case DoorState::INITIALIZING:      next = handleInitializing(evt);     break;
            case DoorState::LOCKED:            next = handleLocked(evt);           break;
            case DoorState::UNLOCKED:          next = handleUnlocked(evt);         break;
            case DoorState::DOOR_OPEN:         next = handleDoorOpen(evt);         break;
            case DoorState::AUTO_LOCK_PENDING: next = handleAutoLockPending(evt);  break;
            case DoorState::ERROR:             next = handleError(evt);            break;
        }
    }

    if (next != _state) {
        onEnterState(next, _state);
        _state = next;
    }
}

// =============================================================================
// Common event handler  (WiFi/MQTT connectivity, buzzer detection, config)
// =============================================================================

bool StateMachine::handleCommon(const DoorEvent& evt, DoorState& next) {
    switch (evt.type) {
        case EventType::WIFI_CONNECTED:
            // Bit is already set by MqttManager. No state change here.
            ESP_LOGI(TAG, "WiFi connected");
            return true;

        case EventType::WIFI_DISCONNECTED:
            if (_state != DoorState::ERROR) {
                _prevState = _state;
                next = DoorState::ERROR;
                cancelAutoLockTimer();
            }
            return true;

        case EventType::MQTT_CONN_UP:
            if (_state == DoorState::INITIALIZING || _state == DoorState::ERROR) {
                // Restore last known good state (or default to LOCKED on first boot).
                next = (_firstMqttConnect) ? DoorState::LOCKED : _prevState;
                _firstMqttConnect = false;
                republishAllStatus();
            }
            return true;

        case EventType::MQTT_CONN_DOWN:
            // MqttManager has already cleared the MQTT bit.
            ESP_LOGW(TAG, "MQTT disconnected");
            return true;

        case EventType::BUZZER_DETECTED:
            setBuzzerActive(true);
            if (millis() - _lastBuzzerNotifyMs >= BUZZER_NOTIFY_COOLDOWN_MS) {
                _lastBuzzerNotifyMs = millis();
                MqttManager::ntfyPost("Building Buzzer", "Your building intercom is ringing!", "urgent");
                ESP_LOGI(TAG, "buzzer detected — ntfy notification sent");
            } else {
                ESP_LOGI(TAG, "buzzer detected — notification suppressed (cooldown)");
            }
            return true;

        case EventType::CMD_SET_CONFIG:
            applyConfig(evt.configKey, evt.configValue);
            return true;

        case EventType::CMD_QUERY_STATUS:
            republishAllStatus();
            return true;

        default:
            return false;
    }
}

// =============================================================================
// State-specific handlers
// =============================================================================

DoorState StateMachine::handleInitializing(const DoorEvent& evt) {
    // In INITIALIZING, we wait for MQTT_CONNECTED (handled by handleCommon).
    // All other events are silently ignored during startup.
    ESP_LOGD(TAG, "INITIALIZING: ignoring event %d", static_cast<int>(evt.type));
    return DoorState::INITIALIZING;
}

DoorState StateMachine::handleLocked(const DoorEvent& evt) {
    switch (evt.type) {
        case EventType::CMD_UNLOCK:
        case EventType::BUTTON_PRESSED:
            _servo->unlockDeadbolt();
            _mqtt->publishLockStatus(false);
            xEventGroupClearBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
            return DoorState::UNLOCKED;

        case EventType::CMD_BUZZER_PRESS:
            _servo->pressBuzzer();
            setBuzzerActive(true);
            return DoorState::LOCKED;

        case EventType::DOOR_OPENED:
            // Anomaly — door opened while locked. Log it and track state.
            ESP_LOGW(TAG, "door opened while locked!");
            MqttManager::ntfyPost("Security", "Door opened while locked!", "high");
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_OPEN);
            _mqtt->publishDoorStatus(true);
            return DoorState::DOOR_OPEN;

        default: return DoorState::LOCKED;
    }
}

DoorState StateMachine::handleUnlocked(const DoorEvent& evt) {
    switch (evt.type) {
        case EventType::CMD_LOCK:
            _servo->lockDeadbolt();
            _mqtt->publishLockStatus(true);
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
            return DoorState::LOCKED;

        case EventType::CMD_BUZZER_PRESS:
            _servo->pressBuzzer();
            setBuzzerActive(true);
            return DoorState::UNLOCKED;

        case EventType::DOOR_OPENED:
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_OPEN);
            _mqtt->publishDoorStatus(true);
            return DoorState::DOOR_OPEN;

        default: return DoorState::UNLOCKED;
    }
}

DoorState StateMachine::handleDoorOpen(const DoorEvent& evt) {
    switch (evt.type) {
        case EventType::DOOR_CLOSED:
            xEventGroupClearBits(_sysEvents, SYS_BIT_DOOR_OPEN);
            _mqtt->publishDoorStatus(false);
            if (!_servo->isDeadboltLocked()) {
                startAutoLockTimer();
                return DoorState::AUTO_LOCK_PENDING;
            }
            return DoorState::LOCKED;

        case EventType::CMD_LOCK:
            _servo->lockDeadbolt();
            _mqtt->publishLockStatus(true);
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
            // Stay in DOOR_OPEN — door is still physically open.
            return DoorState::DOOR_OPEN;

        case EventType::CMD_UNLOCK:
            // Already unlocked — confirm status.
            _mqtt->publishLockStatus(false);
            return DoorState::DOOR_OPEN;

        case EventType::CMD_BUZZER_PRESS:
            _servo->pressBuzzer();
            setBuzzerActive(true);
            return DoorState::DOOR_OPEN;

        default: return DoorState::DOOR_OPEN;
    }
}

DoorState StateMachine::handleAutoLockPending(const DoorEvent& evt) {
    switch (evt.type) {
        case EventType::AUTO_LOCK_TIMER:
            // Timer fired — auto-lock the deadbolt.
            _servo->lockDeadbolt();
            _mqtt->publishLockStatus(true);
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
            xEventGroupClearBits(_sysEvents, SYS_BIT_AUTO_LOCK_PENDING);
            MqttManager::ntfyPost("Auto-Locked", "Door auto-locked after 1 minute.", "default");
            ESP_LOGI(TAG, "auto-lock fired");
            return DoorState::LOCKED;

        case EventType::CMD_LOCK:
            cancelAutoLockTimer();
            _servo->lockDeadbolt();
            _mqtt->publishLockStatus(true);
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
            xEventGroupClearBits(_sysEvents, SYS_BIT_AUTO_LOCK_PENDING);
            return DoorState::LOCKED;

        case EventType::CMD_UNLOCK:
            cancelAutoLockTimer();
            // Already unlocked — just cancel the timer.
            xEventGroupClearBits(_sysEvents, SYS_BIT_AUTO_LOCK_PENDING);
            return DoorState::UNLOCKED;

        case EventType::DOOR_OPENED:
            // Door re-opened during countdown — cancel auto-lock.
            cancelAutoLockTimer();
            xEventGroupSetBits(_sysEvents, SYS_BIT_DOOR_OPEN);
            xEventGroupClearBits(_sysEvents, SYS_BIT_AUTO_LOCK_PENDING);
            _mqtt->publishDoorStatus(true);
            return DoorState::DOOR_OPEN;

        case EventType::CMD_BUZZER_PRESS:
            _servo->pressBuzzer();
            setBuzzerActive(true);
            return DoorState::AUTO_LOCK_PENDING;

        default: return DoorState::AUTO_LOCK_PENDING;
    }
}

DoorState StateMachine::handleError(const DoorEvent& evt) {
    // In ERROR, only local button still works — MQTT commands can't arrive.
    switch (evt.type) {
        case EventType::MQTT_CONN_UP:
            // Handled by handleCommon before reaching here — should not happen.
            return DoorState::ERROR;

        case EventType::BUTTON_PRESSED:
            // Allow manual unlock even while MQTT is down.
            _servo->unlockDeadbolt();
            xEventGroupClearBits(_sysEvents, SYS_BIT_DOOR_LOCKED);
            ESP_LOGI(TAG, "manual unlock during ERROR state");
            return DoorState::ERROR;  // Stay in ERROR until MQTT reconnects.

        default: return DoorState::ERROR;
    }
}

// =============================================================================
// State entry actions
// =============================================================================

void StateMachine::onEnterState(DoorState newState, DoorState oldState) {
    ESP_LOGI(TAG, "state: %d → %d", static_cast<int>(oldState), static_cast<int>(newState));
}

// =============================================================================
// Timer management
// =============================================================================

void StateMachine::startAutoLockTimer() {
    xEventGroupSetBits(_sysEvents, SYS_BIT_AUTO_LOCK_PENDING);
    xTimerChangePeriod(_autoLockTimer,
                       pdMS_TO_TICKS(static_cast<uint32_t>(_autoLockDelayS) * 1000U),
                       0);
    xTimerStart(_autoLockTimer, 0);
    ESP_LOGI(TAG, "auto-lock timer started (%ds)", static_cast<int>(_autoLockDelayS));
}

void StateMachine::cancelAutoLockTimer() {
    xTimerStop(_autoLockTimer, 0);
    xEventGroupClearBits(_sysEvents, SYS_BIT_AUTO_LOCK_PENDING);
    ESP_LOGI(TAG, "auto-lock timer cancelled");
}

void StateMachine::autoLockTimerCallback(TimerHandle_t xTimer) {
    StateMachine* sm = static_cast<StateMachine*>(pvTimerGetTimerID(xTimer));
    DoorEvent evt{};
    evt.type = EventType::AUTO_LOCK_TIMER;
    // Timer task — use xQueueSend (not ISR variant). Timeout 0: don't block the timer task.
    xQueueSend(sm->_eventQueue, &evt, 0);
}

void StateMachine::buzzerLedTimerCallback(TimerHandle_t xTimer) {
    StateMachine* sm = static_cast<StateMachine*>(pvTimerGetTimerID(xTimer));
    xEventGroupClearBits(sm->_sysEvents, SYS_BIT_BUZZER_ACTIVE);
}

// =============================================================================
// Buzzer LED helper
// =============================================================================

void StateMachine::setBuzzerActive(bool active) {
    if (active) {
        xEventGroupSetBits(_sysEvents, SYS_BIT_BUZZER_ACTIVE);
        xTimerReset(_buzzerLedTimer, 0);
    } else {
        xTimerStop(_buzzerLedTimer, 0);
        xEventGroupClearBits(_sysEvents, SYS_BIT_BUZZER_ACTIVE);
    }
}

// =============================================================================
// Status republish  (called on MQTT reconnect and CMD_QUERY_STATUS)
// =============================================================================

void StateMachine::republishAllStatus() {
    const bool locked   = xEventGroupGetBits(_sysEvents) & SYS_BIT_DOOR_LOCKED;
    const bool doorOpen = xEventGroupGetBits(_sysEvents) & SYS_BIT_DOOR_OPEN;
    _mqtt->publishAllStatus(locked, doorOpen);
}

// =============================================================================
// Runtime configuration
// =============================================================================

void StateMachine::applyConfig(ConfigKey key, int32_t value) {
    switch (key) {
        case ConfigKey::AUDIO_THRESHOLD:
            _audio->setThreshold(static_cast<int>(value));
            break;

        case ConfigKey::AUTO_LOCK_DELAY_S:
            _autoLockDelayS = value;
            {
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putInt(NVS_KEY_AUTO_LOCK_DELAY, static_cast<int>(value));
                prefs.end();
            }
            break;

        // Servo angle calibration — writes to NVS via ServoController.
        // Note: setDeadboltAngles(lock, unlock) takes both values simultaneously;
        // passing -1 as the "other" angle is a known limitation to fix post-hardware-test.
        case ConfigKey::SERVO_LOCK_DEG:
            _servo->setDeadboltAngles(static_cast<int>(value), SERVO_DEADBOLT_UNLOCKED_DEG);
            break;
        case ConfigKey::SERVO_UNLOCK_DEG:
            _servo->setDeadboltAngles(SERVO_DEADBOLT_LOCKED_DEG, static_cast<int>(value));
            break;
        case ConfigKey::SERVO_BUZZER_DEG:
            _servo->setBuzzerPressAngle(static_cast<int>(value));
            break;
    }
    ESP_LOGI(TAG, "config applied: key=%d value=%d", static_cast<int>(key), static_cast<int>(value));
}
