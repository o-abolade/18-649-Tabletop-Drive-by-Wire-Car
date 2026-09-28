# 18-649/649 — Tabletop Drive-by-Wire Car

Lab 2: Sensor/Actuator Bring-up. UDP wheel state → Raspberry Pi → serial link → STM32 (Zephyr RTOS) → car sensors/actuators.

```
Logitech wheel --USB--> Laptop proxy --UDP:8000--> Raspberry Pi 4
  + pedals                                              |
                                                       serial
                                                          v
                                                       STM32
                                                    (Zephyr RTOS)
```

## Repo structure

```
18649_lab2/
├── pi/
│   ├── pi_bridge.c        # UDP-to-serial bridge (replaces wheel_monitor.c for real use)
│   ├── wheel_monitor.c     # Part 1 debug tool — prints raw wheel/button state
│   └── state.h             # DIJOYSTATE2_t struct definition
└── stm32/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── boards/
    │   └── nucleo_f401re.overlay   # enables USART1 (PA9/PA10) for the Pi link
    ├── include/
    │   └── pi_stm32_uart.h
    └── src/
        ├── main.c                  # control thread + status-heartbeat thread
        ├── pi_stm32_uart.c         # UART RX state machine, frame parsing, CRC
        └── blinker.c
```

## Part 2 protocol — Pi ↔ STM32 UART frames

Both directions use a fixed 13-byte frame, 115200 baud, 8N1.

**Sync bytes:** `0xAA 0x55` — chosen as bitwise complements of each other with alternating bits, to minimize the chance of payload data accidentally looking like a sync sequence, and to be easy to spot on a scope/logic analyzer.

**CRC:** Zephyr's `crc16_ccitt(seed=0xFFFF, ...)` (see `zephyr/lib/utils/crc16_sw.c` for the reference implementation — **do not** substitute a different CRC16 variant; the Pi-side C port in `pi_bridge.c` must match Zephyr's exact bit-level algorithm, not a generic textbook CRC16-CCITT, or every frame will be rejected).

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

### Status frame (STM32 → Pi), `TYPE = 0x02`

| Offset | Field | Size | Notes |
|---|---|---|---|
| 0–1 | SYNC | 2B | `0xAA 0x55` |
| 2 | TYPE | 1B | `0x02` |
| 3 | SEQ | 1B | mirrors the last received command's SEQ |
| 4 | STATE | 1B | 0=INIT, 1=NORMAL, 2=FAILSAFE, 3=SELFTEST |
| 5–6 | MOTOR1_CURRENT | uint16 LE | placeholder (0) until Part 3 current sensors are wired |
| 7–8 | MOTOR2_CURRENT | uint16 LE | placeholder (0) |
| 9–10 | SERVO_CURRENT | uint16 LE | placeholder (0) |
| 11–12 | CRC16 | uint16 LE | over bytes 2–10 |

### Design notes

- **Framing/resync:** byte-oriented state machine (`SEEK_SYNC0 → SEEK_SYNC1 → COLLECT`), interrupt-driven on the STM32 side via `uart_irq_callback_user_data_set`. A 20ms byte-timeout timer forces a reset back to `SEEK_SYNC0` if a frame stalls mid-collection, so a single dropped byte costs at most one bad frame rather than permanently desyncing the parser.
- **Malformed/out-of-range frames are silently dropped**, not acted on — CRC mismatch or a steering/throttle/brake value outside its documented range both fail closed.
- **No command queueing.** The STM32 keeps only the single latest valid command (spinlock-protected, overwritten on every new valid frame) — a slow control loop never acts on stale backlog.
- **Link-loss failsafe:** the STM32 tracks time since the last valid command; if it exceeds **150ms** (three missed 50ms updates), the zone state moves to `FAILSAFE`.

## Building & running

### STM32 (Zephyr)

```bash
cd stm32
west build -b nucleo_f401re .
west flash
```

Console output (`printk`) is on the ST-Link virtual COM port, 115200 baud — open with PuTTY or similar, separate from the USART1 Pi-link wiring.

> If a from-scratch pristine build races on generated headers (`heap_constants.h: No such file or directory`), do a single-threaded first build:
> ```bash
> CMAKE_BUILD_PARALLEL_LEVEL=1 west build -p always -b nucleo_f401re .
> ```
> Any new `add_library(...)` target added to `CMakeLists.txt` needs `add_dependencies(<target> zephyr_generated_headers)` or it can reintroduce this race.

### Raspberry Pi

Free the GPIO UART from the Linux serial console first (`sudo raspi-config` → Interface Options → Serial Port → login shell: No, hardware: Yes → reboot).

```bash
cd pi
gcc -O2 -Wall -o pi_bridge pi_bridge.c
./pi_bridge /dev/serial0
```

`wheel_monitor` and `pi_bridge` both bind UDP port 8000 — only run one at a time.

### Wiring

- Pi TX → STM32 PA10 (USART1 RX)
- Pi RX → STM32 PA9 (USART1 TX)
- Common ground between Pi and STM32 (required)
- Both sides 3.3V logic

## Status (as of this checkpoint)

**Working / verified:**
- UART link is up in both directions — command frames Pi→STM32 and status heartbeats STM32→Pi, both passing CRC.
- Wheel movement (steering, throttle, brake pedals) tracks correctly end-to-end from the physical wheel through to the STM32's printed command state.
- Byte-timeout resync logic implemented; CRC and range validation implemented and passing.

**Not yet tested:**
- **150ms link-loss failsafe behavior** — logic is implemented (`age > 150ms` → `FAILSAFE` state, printed on transition) but not yet verified against the physical unplug-the-cable checkpoint.
- Status frame's current-sensor fields are still hardcoded to 0 pending Part 3.

**Known minor issue:**
- None outstanding at this checkpoint (earlier `seq` mirroring bug in the status frame has been fixed).

**Not started:**
- Part 3 hardware bring-up (motors/encoders, brake, servo, blinkers, current sensors)
- Part 4 (formal RTOS thread/priority/deadline table)
- Part 5 (final power/wiring pass, test point breakout board)

## Part 3.1 motors and encoders -- software plan

The car harnesses have been broken out onto the breadboard. This is **not** a powered test: the L298N, 12 V supply, and Nucleo signal connections still need to be verified before any motor power is applied.

### Pseudocode

```text
on boot:
  configure all motor PWM outputs disabled
  configure H-bridge direction outputs for the documented safe/brake state
  configure four encoder A/B signals as GPIO interrupt inputs
  set left_count = right_count = 0

on every encoder A or B transition:
  read that encoder's A and B inputs
  compare the new quadrature state with the previous state
  increment or decrement that wheel's atomic transition count
  return immediately; do not print, sleep, or run PID inside an ISR

every fixed control period:
  atomically snapshot and clear each wheel's transition count
  compute left and right speed from transitions / elapsed time
  vehicle_speed = average(left_speed, right_speed)

  if no valid Pi command for more than 150 ms OR brake is active:
    set PWM to zero
    command L298N dynamic-braking input state
  else:
    target_speed = monotonic mapping of throttle (0..1000) to max speed
    error = target_speed - vehicle_speed
    pwm_request = clamp(P gain * error + optional I/D terms, 0, safe maximum)
    write PWM and forward direction to both L298N channels
```

### Wiring boundary

- Each motor's **two motor-power wires** go to one L298N output pair (`OUT1/OUT2` or `OUT3/OUT4`), not to Nucleo GPIO or through the breadboard power rails.
- The Nucleo connects through the breadboard only for low-voltage signals: L298N `ENA/ENB`, `IN1`--`IN4`, encoder power/ground, encoder A/B outputs, and a shared ground reference.
- Do not connect the 12 V adapter until the exact connector pinout, encoder voltage, L298N jumpers, and Nucleo pin assignments have been checked.

## Revision history

- Initial Part 2 implementation and end-to-end bring-up.
