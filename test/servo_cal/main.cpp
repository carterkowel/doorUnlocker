// Servo angle calibration CLI.
// No WiFi needed — just ESP32 + servos + USB.
//
// Serial commands (115200 baud, send with newline):
//   l <deg>   set the stored lock angle (0-180)
//   u <deg>   set the stored unlock angle (0-180)
//   b <deg>   set the stored buzzer press angle (0-180)
//   L         move deadbolt to current lock angle
//   U         move deadbolt to current unlock angle
//   B         trigger buzzer press (move → wait 800ms → return to 90°)
//   s <deg>   sweep deadbolt to any angle (explore without committing)
//   ?         print current stored angles
//
// Procedure:
//   1. Flash this, open Serial Monitor. Type commands, watch servos.
//   2. Use "s <deg>" to sweep until the deadbolt sits right, note the angle.
//   3. Type "l <that angle>" then "L" to confirm it locks.
//   4. Same for unlock and buzzer.
//   5. Update SERVO_DEADBOLT_LOCKED_DEG etc. in config.h with the found values.
//
// Flash: switch VS Code env to "test_servo_cal", click upload.

#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"

static Servo deadbolt;
static Servo buzzer;

static int lockDeg   = SERVO_DEADBOLT_LOCKED_DEG;
static int unlockDeg = SERVO_DEADBOLT_UNLOCKED_DEG;
static int buzzerDeg = SERVO_BUZZER_PRESS_DEG;

static void printHelp() {
    Serial.println("Commands:");
    Serial.println("  l <deg>   store lock angle        L  move to lock");
    Serial.println("  u <deg>   store unlock angle      U  move to unlock");
    Serial.println("  b <deg>   move buzzer to angle    B  full press sequence (move→hold→return)");
    Serial.println("  s <deg>   sweep deadbolt (explore)");
    Serial.println("  ?         print stored angles");
}

static void printStatus() {
    Serial.printf("Stored: lock=%d°  unlock=%d°  buzzer=%d°\n",
                  lockDeg, unlockDeg, buzzerDeg);
}

void setup() {
    Serial.begin(115200);
    deadbolt.attach(PIN_SERVO_DEADBOLT);
    buzzer.attach(PIN_SERVO_BUZZER);
    deadbolt.write(lockDeg);
    buzzer.write(SERVO_BUZZER_IDLE_DEG);
    Serial.println("\n[SERVO CAL] Servo calibration tool ready.");
    printHelp();
    printStatus();
}

void loop() {
    if (!Serial.available()) return;

    const String line = Serial.readStringUntil('\n');
    if (line.length() == 0) return;

    const char cmd = line[0];
    const int  val = (line.length() > 2) ? line.substring(2).toInt() : -1;

    switch (cmd) {
        case 'l':
            if (val < SERVO_DEADBOLT_MIN_DEG || val > SERVO_DEADBOLT_MAX_DEG) {
                Serial.printf("Usage: l <%d-%d>\n", SERVO_DEADBOLT_MIN_DEG, SERVO_DEADBOLT_MAX_DEG); break;
            }
            lockDeg = val;
            Serial.printf("Lock angle stored as %d°  (send 'L' to move)\n", lockDeg);
            break;

        case 'u':
            if (val < SERVO_DEADBOLT_MIN_DEG || val > SERVO_DEADBOLT_MAX_DEG) {
                Serial.printf("Usage: u <%d-%d>\n", SERVO_DEADBOLT_MIN_DEG, SERVO_DEADBOLT_MAX_DEG); break;
            }
            unlockDeg = val;
            Serial.printf("Unlock angle stored as %d°  (send 'U' to move)\n", unlockDeg);
            break;

        case 'b':
            if (val < SERVO_BUZZER_MIN_DEG || val > SERVO_BUZZER_MAX_DEG) {
                Serial.printf("Usage: b <%d-%d>\n", SERVO_BUZZER_MIN_DEG, SERVO_BUZZER_MAX_DEG); break;
            }
            buzzerDeg = val;
            buzzer.write(buzzerDeg);
            Serial.printf("→ buzzer at %d°  (send 'B' for full sweep-press-return sequence)\n", buzzerDeg);
            break;

        case 'L':
            deadbolt.write(lockDeg);
            Serial.printf("→ lock (%d°)\n", lockDeg);
            break;

        case 'U':
            deadbolt.write(unlockDeg);
            Serial.printf("→ unlock (%d°)\n", unlockDeg);
            break;

        case 'B': {
            Serial.printf("→ sweep %d°→%d°, hold %dms, return\n",
                          SERVO_BUZZER_IDLE_DEG, buzzerDeg, SERVO_BUZZER_PRESS_MS);
            const int step = (buzzerDeg < SERVO_BUZZER_IDLE_DEG) ? -1 : 1;
            for (int a = SERVO_BUZZER_IDLE_DEG; a != buzzerDeg; a += step) {
                buzzer.write(a); delay(SERVO_BUZZER_SWEEP_STEP_MS);
            }
            buzzer.write(buzzerDeg);
            delay(SERVO_BUZZER_PRESS_MS);
            for (int a = buzzerDeg; a != SERVO_BUZZER_IDLE_DEG; a -= step) {
                buzzer.write(a); delay(SERVO_BUZZER_SWEEP_STEP_MS);
            }
            buzzer.write(SERVO_BUZZER_IDLE_DEG);
            Serial.println("→ home");
            break;
        }

        case 's': {
            const int clamped = constrain(val, SERVO_DEADBOLT_MIN_DEG, SERVO_DEADBOLT_MAX_DEG);
            if (clamped != val)
                Serial.printf("! clamped %d° to safe range [%d-%d]\n",
                              val, SERVO_DEADBOLT_MIN_DEG, SERVO_DEADBOLT_MAX_DEG);
            deadbolt.write(clamped);
            Serial.printf("→ swept to %d°\n", clamped);
            break;
        }

        case '?':
            printStatus();
            break;

        default:
            printHelp();
            break;
    }
}
