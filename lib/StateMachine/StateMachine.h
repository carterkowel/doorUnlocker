#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>
#include "door_types.h"

// Forward declarations — avoids circular headers
class ServoController;
class MqttManager;
class AudioDetector;

// Central FSM consuming events from the shared event queue.
// All door state transitions and side-effects (servo actuation, MQTT publishes,
// ntfy.sh notifications) originate here.
//
// States:
//   INITIALIZING      Waiting for WiFi+MQTT on first boot.
//   LOCKED            Deadbolt engaged, door closed.
//   UNLOCKED          Deadbolt retracted, door closed.
//   DOOR_OPEN         Door physically open (reed switch).
//   AUTO_LOCK_PENDING Door closed after being opened while unlocked; timer running.
//   ERROR             WiFi/MQTT connection lost.
class StateMachine {
public:
    StateMachine() = default;

    void begin(QueueHandle_t eventQueue,
               EventGroupHandle_t sysEvents,
               ServoController& servo,
               MqttManager& mqtt,
               AudioDetector& audio);

    // Process one event from the queue (blocks up to portMAX_DELAY).
    // Call in a tight loop from the state machine task.
    void run();

    DoorState getState() const { return _state; }

private:
    // Per-state event handlers — each returns the next state.
    DoorState handleInitializing(const DoorEvent& evt);
    DoorState handleLocked(const DoorEvent& evt);
    DoorState handleUnlocked(const DoorEvent& evt);
    DoorState handleDoorOpen(const DoorEvent& evt);
    DoorState handleAutoLockPending(const DoorEvent& evt);
    DoorState handleError(const DoorEvent& evt);

    // Events handled identically in all states.
    bool handleCommon(const DoorEvent& evt, DoorState& next);

    void onEnterState(DoorState newState, DoorState oldState);
    void startAutoLockTimer();
    void cancelAutoLockTimer();
    void applyConfig(ConfigKey key, int32_t value);
    void republishAllStatus();

    static void autoLockTimerCallback(TimerHandle_t xTimer);
    static void buzzerLedTimerCallback(TimerHandle_t xTimer);

    void setBuzzerActive(bool active);

    QueueHandle_t      _eventQueue = nullptr;
    EventGroupHandle_t _sysEvents  = nullptr;
    ServoController*   _servo      = nullptr;
    MqttManager*       _mqtt       = nullptr;
    AudioDetector*     _audio      = nullptr;

    DoorState     _state             = DoorState::INITIALIZING;
    DoorState     _prevState         = DoorState::LOCKED;
    TimerHandle_t _autoLockTimer     = nullptr;
    TimerHandle_t _buzzerLedTimer    = nullptr;
    int32_t       _autoLockDelayS    = 60;
    bool          _firstMqttConnect  = true;
    uint32_t      _lastBuzzerNotifyMs = 0;
};
