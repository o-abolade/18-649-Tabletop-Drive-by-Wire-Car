# 18-649/649 — Tabletop Drive-by-Wire Car

Lab 2: Sensor/Actuator Bring-up. UDP wheel state → Raspberry Pi → serial link →
STM32 (Zephyr RTOS) → car sensors/actuators.

**Status: complete (final code: commit `56a3cfe`, 2026-10-07).** This folder was
renamed from `18649_lab2/` to `lab2_sensors_actuators/` when Lab 3 started.
Lab 3 lives in `../lab3_CAN/`.

```
Logitech wheel --USB--> Laptop proxy --UDP:8000--> Raspberry Pi 4
  + pedals                                              |
                                                       serial (USART1, 115200 8N1)
                                                          v
                                                       STM32 Nucleo-F401RE
                                                    (Zephyr RTOS)
                                                          |
                     2 motors + encoders, steering servo, 4 blinkers, 3 current sensors
```

## Repo structure

```
lab2_sensors_actuators/
├── pi/
│   ├── pi_bridge.c        # UDP-to-serial bridge, test points, --invalid-test mode
│   └── state.h            # DIJOYSTATE2_t struct definition
└── stm32/
    ├── CMakeLists.txt     # one add_library() per module (see Building)
    ├── prj.conf
    ├── boards/
    │   └── nucleo_f401re.overlay
    ├── include/           # one header per module
    └── src/
        ├── main.c             # control thread, status/heartbeat thread, PID, failsafe, self-test
        ├── pi_stm32_uart.c    # interrupt-driven UART RX state machine, CRC, command semaphore
        ├── blinker.c          # raw GPIO driver for the 4 blinker LEDs
        ├── blinker_ctrl.c     # blink state machine, self-cancel, hazard (own thread)
        ├── steering.c         # servo PWM mapping (asymmetric left/right scale)
        ├── motor_control.c    # L298N H-bridge driver, dynamic brake
        ├── encoder.c          # x4 quadrature decoding on GPIO interrupts
        ├── pid_controller.c   # generic PID
        ├── current.c          # ACS712 current sensing over ADC
        ├── testpoints.c       # synthetic GPIO test points (CMD_RX, PWM_SET)
        └── bench_commands.c   # shell commands for bench testing
```

## Pi ↔ STM32 UART protocol

115200 baud, 8N1. Command frames are 13 bytes, status frames are 19 bytes.

**Sync bytes:** `0xAA 0x55`. They are bitwise complements of each other with
alternating bits, to make it unlikely that payload data looks like a sync
sequence, and to be easy to spot on a scope/logic analyzer.

**CRC:** Zephyr's `crc16_ccitt(seed=0xFFFF, ...)` (`zephyr/lib/utils/crc16_sw.c`).
The Pi-side C version in `pi_bridge.c` is a line-for-line port. A generic
textbook CRC16-CCITT gives different results and every frame gets rejected.

### Command frame (Pi → STM32), `TYPE = 0x01`

| Offset | Field | Size | Notes |
|---|---|---|---|
| 0–1 | SYNC | 2B | `0xAA 0x55` |
| 2 | TYPE | 1B | `0x01` |
| 3 | SEQ | 1B | rolling counter |
| 4–5 | STEERING | int16 LE | signed, range **±1000** |
| 6–7 | THROTTLE | uint16 LE | range **0–1000** |
| 8–9 | BRAKE | uint16 LE | range **0–1000** |
| 10 | BUTTONS | 1B bitmask | bit0=left blink, bit1=right blink, bit2=self-test |
| 11–12 | CRC16 | uint16 LE | over bytes 2–10 |

Sent at least every 50ms (`SEND_PERIOD_MS`). The last values are re-sent if no
new UDP packet arrived.

### Status frame (STM32 → Pi), `TYPE = 0x02`

| Offset | Field | Size | Notes |
|---|---|---|---|
| 0–1 | SYNC | 2B | `0xAA 0x55` |
| 2 | TYPE | 1B | `0x02` |
| 3 | SEQ | 1B | mirrors the last received command's SEQ |
| 4 | STATE | 1B | 0=INIT, 1=NORMAL, 2=FAILSAFE, 3=SELFTEST |
| 5–8 | MOTOR1_CURRENT | float32 LE | amps |
| 9–12 | MOTOR2_CURRENT | float32 LE | amps |
| 13–16 | SERVO_CURRENT | float32 LE | amps |
| 17–18 | CRC16 | uint16 LE | over bytes 2–16 |

Sent every 20ms (status thread). Grew from 13 to 19 bytes when current sensing
switched to float32. Both sides had to change together.

### Design notes

- **Framing/resync:** byte-oriented state machine (`SEEK_SYNC0 → SEEK_SYNC1 → COLLECT`),
  interrupt-driven on the STM32 via `uart_irq_callback_user_data_set`. A 20ms
  byte-timeout timer resets to `SEEK_SYNC0` if a frame stalls mid-collection, so
  a dropped byte costs at most one frame.
- **Invalid frames:** CRC mismatches are dropped. A valid-CRC frame with
  out-of-range values sends the STM32 to `FAILSAFE` (exercised with
  `pi_bridge --invalid-test`, which sends throttle=1001 after 1s of valid traffic
  once the link reaches NORMAL).
- **No command queueing.** The STM32 keeps only the latest valid command
  (spinlock-protected), so the control loop never acts on stale backlog.
- **Link-loss failsafe:** if no valid command arrives for **100ms**
  (`FAILSAFE_TIMEOUT_MS`, tightened from 150ms), the STM32 enters `FAILSAFE`:
  dynamic brake + hazards. It recovers automatically when valid commands resume.

## Control architecture (STM32)

| Thread / ISR | Priority | Wakes on | Job |
|---|---|---|---|
| UART RX ISR | IRQ | each byte | parse frames, store latest command, give `cmd_received_sem` |
| Control thread | 5 | `cmd_received_sem`, or 10ms timeout | failsafe/self-test, steering, brake, throttle, button edges |
| Blinker thread | 6 | its own `k_sleep` timing | 1 Hz blink / 2 Hz hazard, self-cancel |
| Status thread | 7 | 20ms | read current sensors, send status frame |

- The control thread is **semaphore-driven**, so throttle/brake react within a
  fraction of a millisecond instead of up to a 10ms poll.
- **PID** (`Kp=0.15, Ki=0.04, Kd=0`, one controller per wheel) is rate-gated to
  `PID_UPDATE_PERIOD_MS = 20ms` and uses real elapsed `dt` from `k_uptime_get()`.
  Differentiating encoder counts over ~1ms is pure quantization noise.
- `MOTOR_MIN_EFFECTIVE_DUTY = 55`: nonzero duty is clamped up to the measured
  breakaway threshold so low throttle doesn't buzz without turning.
- **Self-test button:** single press → failsafe (hazards + brake); second press
  within 500ms → back to normal. The edge is evaluated before the failsafe
  branch, otherwise the second press is never seen.
- Debug `printk` of the received command is rate-limited to every 100ms. Printing
  every command overflowed the log buffer (`--- N messages dropped ---`).

## Building & running

### STM32 (Zephyr)

```bash
west build -b nucleo_f401re lab2_sensors_actuators/stm32 -d lab2_sensors_actuators/stm32/build
west flash -d lab2_sensors_actuators/stm32/build
```

Console output (`printk` and the shell) is on the ST-Link virtual COM port
(USART2), 115200 baud.

> If a pristine build races on generated headers (`heap_constants.h: No such file or directory`),
> do a single-threaded first build: `CMAKE_BUILD_PARALLEL_LEVEL=1 west build -p always ...`.
> Every `add_library(...)` target in `CMakeLists.txt` needs
> `add_dependencies(<target> zephyr_generated_headers)`.

### Raspberry Pi

Free the GPIO UART from the Linux serial console first (`sudo raspi-config` →
Interface Options → Serial Port → login shell: No, hardware: Yes → reboot).

```bash
cd pi
gcc -O2 -Wall -o pi_bridge pi_bridge.c -lgpiod
./pi_bridge /dev/serial0                  # normal
./pi_bridge /dev/serial0 --invalid-test   # inject an out-of-range frame after 1s
```

### Bench shell commands (STM32 console)

```text
motor_pulse <a|b>          # raw motor output test (steps through duties), no PID, no min-duty floor
steering_test              # center / right / left servo test
pid_enable <0|1>           # enable PID after encoder validation
pid_gains <kp> <ki> <kd>
encoder_status | encoder_zero | encoder_levels | encoder_edges
encoder_monitor | encoder_count_monitor
```

Don't run bench commands while `pi_bridge` is live. `steering_test` isn't
interlocked against the control thread.

## Pin map (final overlay)

| Function | Pin | Peripheral / notes |
|---|---|---|
| Pi link TX / RX | PA9 (D8) / PA10 (D2) | USART1 |
| Console / shell | PA2 / PA3 | USART2, hardwired to ST-Link VCP. Never reassign |
| Blinker FL / FR | PB9 (D14) / PB8 (D15) | GPIO |
| Blinker RL / RR | PC7 (D9) / PB6 (D10) | GPIO |
| Motor left PWM (ENA) | PA0 (A0) | TIM2 CH1 |
| Motor right PWM (ENB) | PA7 (D11) | TIM1 CH1N (complementary). PA8 is taken by an encoder |
| Motor left IN1 / IN2 | PC1 (A4) / PC0 (A5) | GPIO |
| Motor right IN3 / IN4 | PB5 (D4) / PB3 (D3) | GPIO. **IN4 is the pin that differs between forward and brake** |
| Servo PWM | PA6 (D12) | TIM3 CH1, prescaler 27 → 3 MHz, 20ms period |
| Encoder right A / B | PA8 (D7) / PA5 (D13) | GPIO interrupts |
| Encoder left A / B | PB4 (D5) / PB10 (D6) | GPIO interrupts |
| Current: left motor / right motor / servo | PA4 (A2) / PB0 (A3) / PA1 (A1) | ADC1 IN4 / IN8 / IN1 |
| Test point CMD_RX | PC8 | pulses on each valid command frame |
| Test point PWM_SET | PC6 | pulses on each duty write (twice per PID cycle, once per motor) |
| Pi test point UDP_RX | BCM GPIO 18 (pin 12) | pulses on each UDP datagram |
| Pi test point CMD_TX | BCM GPIO 27 (pin 13) | pulses after each serial command write |

Forward: IN1=0, IN2=1 (left); IN3=1, IN4=0 (right). Brake: all IN high, both
enables at 100% (dynamic braking: resists motion, not a mechanical lock).
`&spi1`, `&i2c1`, `&i2c3` are disabled to free their pins. Pi and STM32 share
ground; both are 3.3V logic.

## Measured results

| Requirement | Test points | Target | Measured |
|---|---|---|---|
| Brake (R2.2) | CMD_RX → DIR_A (IN4, PB3) | ≤2ms | 0.3ms ✅ |
| Throttle software latency (R2.1) | CMD_RX → PWM_SET (first pulse) | ≤2ms | 0.15ms ✅ |
| Throttle actuator response | PWM_SET → PWM_OUT (PA0) | duty change | Confirmed qualitatively ✅ |

## Final status

**Working:**
- UART link both directions with CRC, resync, range checking and the 100ms
  link-loss failsafe.
- Steering servo, four blinkers (turn signals, self-cancel, hazards), closed-loop
  motor speed control with dynamic braking, encoders, current sensing in the
  status frame.
- Self-test button: single press → failsafe, double press → restore.
- Pi and STM32 test points for the Part 5 timing measurements.

**Known limitations carried forward:**
- PID gains were tuned before the 20ms PID rate-gate was added. Re-validate them
  if the update rate changes.
- `steering_test` isn't interlocked against the live control loop.
- `motor_pulse` at 50% duty doesn't turn the wheel. That's below the motor's
  breakaway threshold, and the bench command intentionally skips
  `MOTOR_MIN_EFFECTIVE_DUTY`.

## Revision history

- Initial Part 2 implementation and end-to-end bring-up.
- Recorded `part-3.1` motor/encoder bring-up, L298N/encoder pin map, diagnostic
  commands, and the encoder-signal blocker (later resolved).
- 2026-10-01: test points, self-test fix, invalid-frame test mode, failsafe
  tightened to 100ms.
- 2026-10-07: final Lab 2 commit (`56a3cfe`): rate-limited debug prints.
- 2026-10-19: folder renamed to `lab2_sensors_actuators/`; this doc updated to
  the final state.
