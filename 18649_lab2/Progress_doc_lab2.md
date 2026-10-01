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

- Pi TX → STM32 D2 / PA10 (USART1 RX)
- Pi RX ← STM32 D8 / PA9 (USART1 TX)
- Common ground between Pi and STM32 (required)
- Both sides 3.3V logic

The ST-Link console remains on USART2 (D0/D1). The Pi protocol uses USART1
at 115200 baud, 8N1; binary status frames should not appear on the console.
The remap moved the right motor ENB PWM from D2 to D11 / PA7
(TIM1_CH1N, complementary channel 1), and right encoder phase B from D8 to
D13 / PA5. The D11 complementary output uses normal polarity so a 100% PWM
request drives ENB high. D13 must be wired to the encoder signal, not motor power.

## Status (as of this checkpoint)

**Working / verified:**
- UART link is up in both directions — command frames Pi→STM32 and status heartbeats STM32→Pi, both passing CRC.
- Wheel movement (steering, throttle, brake pedals) tracks correctly end-to-end from the physical wheel through to the STM32's printed command state.
- Byte-timeout resync logic implemented; CRC and range validation implemented and passing.
- LED driver code implemented and tested - still need to write code to ensure requirements are met
- Steering servo driver code implemented and tested

**Not yet tested:**
- **150ms link-loss failsafe behavior** — logic is implemented (`age > 150ms` → `FAILSAFE` state, printed on transition) but not yet verified against the physical unplug-the-cable checkpoint.
- Status frame's current-sensor fields are still hardcoded to 0 pending Part 3.
- Current sensor code written, still needs testing

**Current blocker:**
- Motor PWM and direction output have been verified on the `part-3.1` branch,
  but neither six-wire motor encoder has produced repeatable quadrature counts.
  The STM32 GPIO interrupt and decoder paths pass controlled grounding tests,
  so the motor connector pinout, Hall-sensor supply voltage, and A/B waveforms
  are the next hardware checks.

**In progress on `part-3.1`:**
- Safe L298N initialization, bounded motor pulse tests, quadrature decoding,
  raw edge diagnostics, and continuous encoder shell monitors.
- The final direction-pin change moves `IN4` away from ADC pin A3/PB0 to
  Arduino-header D10/PB6.

**Not started:**
- Closed-loop motor speed control and brake-state verification
- Part 4 (formal RTOS thread/priority/deadline table)
- Part 5 (final power/wiring pass, test point breakout board)

## Current motor and encoder pin mapping

The table below follows the current STM32 overlay. Channel A drives the left
wheel through OUT1/OUT2; channel B drives the right wheel through OUT3/OUT4.
At startup, during failsafe, and at zero throttle, the controller requests
dynamic braking: IN1--IN4 high and both enables at 100%. This resists wheel
motion while the bridge has power; it is not a mechanical wheel lock.

### L298N control pins

| L298N signal | Nucleo pin | Purpose |
|---|---|---|
| ENA | A0 / PA0 / TIM2_CH1 | channel A PWM; physical left wheel |
| IN1 | A4 / PC1 | channel A direction |
| IN2 | A5 / PC0 | channel A direction |
| IN3 | D4 / PB5 | channel B direction |
| IN4 | D3 / PB3 | channel B direction |
| ENB | D11 / PA7 / TIM1_CH1N | channel B PWM; physical right wheel |

Forward direction: IN1=0, IN2=1 for the left wheel; IN3=1, IN4=0 for the
right wheel. Reverse swaps each pair.

SPI1 is disabled because its default pins overlap application pins. The
current-sensor inputs are A2/PA4 (left), A3/PB0 (right), and A1/PA1 (servo).

### Encoder pins and diagnostics

| Encoder signal | Nucleo pin |
|---|---|
| Right A | D7 / PA8 |
| Right B | D13 / PA5 |
| Left A | D5 / PB4 |
| Left B | D6 / PB10 |

Available shell diagnostics on the branch are:

```text
encoder_levels          # one live A/B sample
encoder_edges           # accumulated raw GPIO edges
encoder_status          # signed quadrature counts
encoder_zero            # reset counts and raw edges
encoder_monitor         # continuous A/B levels until Ctrl-C
encoder_count_monitor   # continuous signed/raw counts until Ctrl-C
```

The GPIO inputs use pull-ups and controlled input sequences have verified the
decoder. Direct motor-connector testing should measure each A/B channel
relative to encoder ground while verifying the encoder's rated VCC. The
current 3.3 V supply might be below the minimum for some common six-wire Hall
encoders, so the exact motor/encoder part number must be confirmed before
changing the supply or connecting any 5 V output to the STM32.

## Revision history

- Initial Part 2 implementation and end-to-end bring-up.
- Recorded `part-3.1` motor/encoder bring-up, final L298N/encoder pin map,
  diagnostic commands, and the unresolved encoder-signal blocker.
