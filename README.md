# doorUnlocker

ESP32-based apartment access control. Lock and unlock your deadbolt from your phone over the internet, get push notifications when your building intercom buzzes, and auto-lock after the door closes.

## Hardware

| Component | Part | GPIO |
|---|---|---|
| Microcontroller | ESP32-WROOM-32 (CP2102) | — |
| Deadbolt servo | Any 5V servo | 18 |
| Buzzer button servo | Any 5V servo | 19 |
| Door reed switch | NC/NO reed switch | 34 |
| Manual unlock button | Momentary pushbutton | 35 |
| Microphone | Electret MAX4466 module | 36 (ADC1) |
| WiFi/MQTT LED | LED + 330Ω | 25 |
| Door state LED | LED + 330Ω | 26 |
| Lock state LED | LED + 330Ω | 27 |
| Buzzer activity LED | LED + 330Ω | 14 |

**Wiring notes:**
- GPIO 34 and 35 are input-only — add external 10kΩ pull-ups to 3.3V for the reed switch and button
- GPIO 36 (VP) is ADC1 — never use ADC2 pins for audio; ADC2 is shared with the WiFi RF switch
- MAX4466 powers from 3.3V; its output sits at VCC/2 (~1.65V) with audio on top — this is the expected DC bias

## Remote Access Stack

| Concern | Tool | Cost |
|---|---|---|
| Lock/unlock commands | MQTT — [HiveMQ Cloud](https://console.hivemq.cloud) free tier | Free forever |
| Phone control UI | Progressive Web App on GitHub Pages | Free |
| Push notifications | [ntfy.sh](https://ntfy.sh) | Free |

**Why this stack:** HiveMQ Cloud free tier gives you a TLS-encrypted broker with 100 connections and no credit card. ntfy.sh delivers phone push notifications even when the PWA is backgrounded — Web Push from a static PWA alone requires a relay server. Telegram was rejected because its polling model adds 1–3s latency; unacceptable for a door lock.

## Setup

### 1. Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- HiveMQ Cloud account — [console.hivemq.cloud](https://console.hivemq.cloud) (free, no credit card)
- ntfy.sh — install the iOS or Android app and choose a unique private topic name

### 2. Credentials
```bash
cp include/secrets.h.example include/secrets.h
```
Edit `include/secrets.h` with your WiFi SSID/password, HiveMQ cluster host/credentials, and your ntfy.sh topic. This file is gitignored and must never be committed.

On HiveMQ Cloud, create an Access Management credential with publish/subscribe permission on `door/apt1/#` only — do not use the admin credential in firmware.

### 3. Build and flash
```bash
pio run --target upload
pio device monitor   # 115200 baud
```

On first boot you should see all five FreeRTOS tasks start, WiFi connect, and MQTT connect to HiveMQ. The lock state LED turns solid (deadbolt initialises locked).

## Commissioning

### Servo calibration
The default angles (lock = 0°, unlock = 90°, buzzer press = 60°) are almost certainly wrong for your hardware. Calibrate by sending MQTT config commands from the HiveMQ Cloud console:

```
Topic:   door/apt1/cmd/config
Payload: {"key":"servo_unlock_deg","value":95}
```

Available config keys: `servo_lock_deg`, `servo_unlock_deg`, `servo_buzzer_deg`

Values are written to NVS immediately and survive power cycles.

### Audio threshold calibration
The Goertzel detector evaluates power at 843.75 Hz (nearest bin to 840 Hz at 4 kHz sample rate / 128-sample blocks). Default threshold is 1500 — likely needs tuning.

1. Open the serial monitor (115200 baud)
2. Play the building buzzer or a 840 Hz tone near the microphone
3. Watch for `last_power=XXXX` log lines from `AudioDet`
4. Set the threshold halfway between the ambient noise floor and the buzzer peak:

```
Topic:   door/apt1/cmd/config
Payload: {"key":"audio_threshold","value":2500}
```

## MQTT Topic Reference

All topics are prefixed `door/apt1/`.

### Commands → ESP32

| Topic | Payload | Effect |
|---|---|---|
| `cmd/deadbolt` | `LOCK` / `UNLOCK` | Actuate deadbolt servo |
| `cmd/buzzer` | `PRESS` | Press building buzzer button servo |
| `cmd/query` | `STATUS` | Republish all retained status topics |
| `cmd/config` | `{"key":"…","value":…}` | Update a runtime parameter |

### Status ← ESP32 (retained, QoS 1)

| Topic | Values | Notes |
|---|---|---|
| `status/lock` | `LOCKED` / `UNLOCKED` | Current deadbolt position |
| `status/door` | `OPEN` / `CLOSED` | Reed switch state |
| `status/online` | `online` / `offline` | Last Will Testament — broker publishes `offline` on unexpected disconnect |
| `status/wifi_rssi` | e.g. `-65` (dBm) | Published every 60 s |

### Push notifications via ntfy.sh

| Trigger | Priority |
|---|---|
| Building buzzer detected | urgent |
| Door opened while locked (anomaly) | high |
| Auto-lock fired | default |

## LED Reference

| LED | GPIO | Solid on | Slow blink (1 Hz) | Fast blink (5 Hz) | Pulse (2 Hz) |
|---|---|---|---|---|---|
| WiFi/MQTT | 25 | WiFi + MQTT connected | WiFi only, no MQTT | No WiFi | — |
| Door | 26 | Door closed | — | — | — |
| Lock | 27 | Locked | — | — | Auto-lock countdown |
| Buzzer | 14 | Buzzer tone active | — | — | — |

## Architecture

### FreeRTOS task layout

```
Core 0 (Protocol CPU)          Core 1 (Application CPU)
──────────────────────         ────────────────────────
taskWifiMqtt    pri=5          taskAudioProc    pri=5
taskLedManager  pri=2          taskStateMachine pri=4
                               taskPeripherals  pri=3
```

All cross-task communication uses FreeRTOS primitives — no shared globals:
- **`g_eventQueue`** — `DoorEvent` structs from all producers → StateMachine (sole consumer)
- **`g_sysEvents`** — event group bits read by LedManager; WiFi/MQTT bits written by MqttManager, door/lock/buzzer bits written by StateMachine
- **`g_servoMutex`** — serialises concurrent servo access

### Audio sampling

An `esp_timer` fires at 4 kHz (250 µs period). The callback calls `analogRead()` and posts the sample to an internal queue. `taskAudioProc` blocks on that queue and runs the Goertzel recurrence — avoiding any busy-wait loop that would starve `taskStateMachine` on Core 1. Three consecutive 128-sample blocks above threshold (~96 ms of sustained tone) confirms a buzzer detection.

### Door state machine

```
INITIALIZING ──── MQTT connected ────► LOCKED
                                          │
                        CMD_UNLOCK / BUTTON_PRESSED
                                          │
                                       UNLOCKED
                                          │
                                     Door opened
                                          │
                                       DOOR_OPEN
                                          │
                                     Door closed
                                          │
                                  AUTO_LOCK_PENDING  ◄── 60 s timer
                                      │       │
                               CMD_LOCK    Timer fires
                                  │             │
                               LOCKED ◄─────────┘

Any state ──── WiFi lost ────► ERROR ──── MQTT reconnected ────► (restore prior state)
```

Manual button works in all states including ERROR (MQTT down).

### Library components

| Component | Responsibility |
|---|---|
| `AudioDetector` | Goertzel 840 Hz detection, 4 kHz esp_timer, 128-sample blocks |
| `ServoController` | ESP32Servo (LEDC PWM), NVS angle calibration, mutex-guarded actuation |
| `MqttManager` | PubSubClient + WiFiClientSecure TLS, exponential-backoff reconnect, ntfy.sh HTTP POST |
| `StateMachine` | Door FSM, auto-lock FreeRTOS timer, buzzer LED timer |
| `LedManager` | Tick-based non-blocking LED patterns at 50 ms update rate |
| `ReedSwitch` | Debounced GPIO polling (100 ms), posts `DOOR_OPENED`/`DOOR_CLOSED` |
| `ButtonHandler` | Debounced falling-edge detection (50 ms), posts `BUTTON_PRESSED` |

### Runtime configuration

Tunable parameters (servo angles, audio threshold, auto-lock delay) live in NVS namespace `door_cfg`. Defaults are in `include/config.h`; any change via `cmd/config` is saved to NVS immediately and persists across reboots. Credentials are compile-time constants in `include/secrets.h` (gitignored).

## Planned: PWA Frontend

A Progressive Web App on GitHub Pages will connect to HiveMQ via MQTT over WebSocket (port 8884), show real-time lock and door state, and provide one-tap servo control from the phone home screen. Push notifications for the buzzer go through ntfy.sh rather than the PWA because Web Push from a static site requires a relay server.
