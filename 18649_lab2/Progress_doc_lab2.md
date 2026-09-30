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
    │   ├── current.h
    │   ├── encoder.h
    │   ├── motor_control.h
    │   └── pi_stm32_uart.h
    └── src/
        ├── current.c                # ADC current-sense acquisition
        ├── main.c                  # control thread + status-heartbeat thread
        ├── encoder.c                # quadrature decoder + raw edge diagnostics
        ├── motor_control.c          # safe L298N outputs + bounded bench pulse
        ├── pid_controller.c          # PID implementation; disabled until encoder validation
        ├── pi_stm32_uart.c         # UART RX state machine, frame parsing, CRC
        ├── steering.c                # steering-servo PWM driver
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
- Current-sensor calibration remains unverified; status fields presently carry
  raw 12-bit ADC samples rather than calibrated current.

**Current blocker:**
- Neither motor encoder has yet produced a trustworthy signed wheel count.
  The Nucleo GPIO/interrupt paths and right quadrature decoder pass controlled
  input tests, but the signals from the motor-side harnesses remain unverified.
  This is not yet a confirmed failed encoder or a confirmed firmware fault;
  details are recorded below.

**In progress:**
- Part 3.1 motor control: L298N outputs initialize in a safe coast state,
  then valid throttle commands use the verified forward direction and PWM.
  Brake and link-loss paths request dynamic braking; these electrical states
  still require wheels-raised verification.
- Part 3.1 encoder bring-up: firmware records raw A/B edges, decodes valid
  quadrature transitions into signed counts, and exposes `encoder_status`,
  `encoder_zero`, `encoder_levels`, `encoder_edges`, `encoder_monitor`, and
  `encoder_count_monitor` on the STM32 console. Grounding the left D3/D11 paths
  produced raw edges. A controlled four-transition test on right D12/D14
  produced a signed count of -4, verifying the right decoder and GPIO path.
  In contrast, motor-driven pulses have produced uneven or intermittent raw
  A/B edges but signed counts remain zero; right motor pulses have produced
  no right encoder edges. Connector mapping and signal quality remain in
  progress. The PID controller is coded but deliberately disabled by default
  until both encoders produce valid, repeatable signed counts.
- Steering, blinkers, and raw current-sensor sampling are integrated into the
  control/status threads. Their hardware calibration and final wiring remain
  to be verified.

**Not started:**
- PID gain tuning after encoder repair; encoder scaling/sign calibration
- Dynamic-brake, steering, blinker, and current-sensor hardware verification
- Part 4 (formal RTOS thread/priority/deadline table)
- Part 5 (final power/wiring pass, test point breakout board)

## Part 3.1 motors and encoders -- software plan

The motor path has passed its bounded, wheels-raised pulse test and the encoder
harnesses are wired. Normal throttle currently uses open-loop PWM so the lab
can proceed. The software PID path samples encoder count deltas at 10 ms, but
it remains disabled until both encoder signal pairs pass the wheel-count check.

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
- The 12 V adapter connects directly to the L298N motor-power input only. It
  has been used for bounded, wheels-raised motor pulses. Keep it disconnected
  when moving wires or running the USB-only GPIO tests. The encoder Hall
  supply is currently the separate Nucleo 3.3 V `VCC`/`GND` pair, but the
  exact encoder model and its minimum supply voltage still need verification;
  several common six-wire Hall encoders require more than 3.3 V. A 12 V motor
  supply is needed only for the bounded motor-driven pulse tests.

### Planned L298N control mapping (wiring remap in progress)

| L298N signal | Nucleo pin | Purpose |
|---|---|---|
| ENA | A0 / PA0 / TIM2_CH1 | L298N channel A PWM; physical left wheel (`OUT1/OUT2`) |
| IN1 | A4 / PC1 | channel A direction |
| IN2 | A5 / PC0 | channel A direction |
| IN3 | D4 / PB5 | channel B direction |
| IN4 | D3 / PB3 | channel B direction |
| ENB | D2 / PA10 / TIM1_CH3 | L298N channel B PWM; physical right wheel (`OUT3/OUT4`) |

### Complete Nucleo wiring map

This table is the planned wiring target during the remap. The firmware still
uses the earlier L298N pin assignments until its devicetree and source are
updated. Entries marked **TBD** were displaced by the requested motor remap
and must receive new pins before those subsystems are rewired. All low-voltage
devices in the table (Nucleo, Pi, L298N logic, encoders, current sensors, servo, and
blinkers) require a **common ground**.  Pin labels use the Nucleo Arduino
headers followed by the STM32 pin and peripheral where applicable.

| Function | External connection | Nucleo pin | Wiring / implementation note |
|---|---|---|---|
| L298N channel A enable | `ENA` | `A0 / PA0 / TIM2_CH1` | Planned PWM target (Zephyr `pwm2`, channel 1); channel A is the physical left wheel through `OUT1/OUT2`. PWM peripheral setup remains a firmware-remap task. Remove the `ENA` jumper before connecting this signal. |
| L298N channel A direction | `IN1` | `A4 / PC1` | Planned pin. Re-verify left-wheel forward polarity after the output-pair change. |
| L298N channel A direction | `IN2` | `A5 / PC0` | Planned pin. Re-verify left-wheel forward polarity after the output-pair change. |
| L298N channel B direction | `IN3` | `D4 / PB5` | Planned pin. Re-verify right-wheel forward polarity after the output-pair change. |
| L298N channel B direction | `IN4` | `D3 / PB3` | Planned pin. Re-verify right-wheel forward polarity after the output-pair change. |
| L298N channel B enable | `ENB` | `D2 / PA10 / TIM1_CH3` | Planned PWM target (Zephyr `pwm1`, channel 3); channel B is the physical right wheel through `OUT3/OUT4`. PWM peripheral setup remains a firmware-remap task. Remove the `ENB` jumper before connecting this signal. |
| Left drive motor | L298N `OUT1`, `OUT2` | — | Planned connection. Connect only to the two left-motor power wires. Swapping the pair reverses physical direction. |
| Right drive motor | L298N `OUT3`, `OUT4` | — | Planned connection. Connect only to the two right-motor power wires. Swapping the pair reverses physical direction. |
| Motor power | L298N motor supply (`Vs` / `+12V`) and L298N GND | — | 12 V adapter goes to the L298N only, never a Nucleo GPIO or 3.3 V/5 V pin. Disconnect it before moving wires. |
| Left encoder phase A | Motor encoder `A` | `D5 / PB4` | Planned GPIO input. |
| Left encoder phase B | Motor encoder `B` | `D6 / PB10` | Planned GPIO input. |
| Right encoder phase A | Motor encoder `A` | `D7 / PA8` | Planned GPIO input. |
| Right encoder phase B | Motor encoder `B` | `D8 / PA9` | Planned GPIO input. |
| Encoder supply | Encoder `VCC` | Nucleo `3V3` | Current test wiring uses 3.3 V. Verify the encoder's exact supply requirement before assuming 3.3 V is valid. |
| Encoder return | Encoder `GND` | Nucleo `GND` | This ground must also be tied to the L298N/Nucleo signal ground. |
| Steering servo control | Servo PWM signal | `D12 / PA6 / TIM3_CH1` | Planned PWM target (Zephyr `pwm3`, channel 1), 20 ms period. Displaces right encoder phase A; firmware setup remains a later remap task. |
| Front-left blinker | FL control input | `D14 / PB9` | Planned GPIO output. |
| Front-right blinker | FR control input | `D15 / PB8` | Planned GPIO output. |
| Rear-left blinker | RL control input | `D9 / PC7` | Planned GPIO output. |
| Rear-right blinker | RR control input | `D10 / PB6` | Planned GPIO output. |
| Right-motor current sense | Current-sensor analog output | `A3 / PB0 / ADC1_IN8` | Firmware reports raw 12-bit ADC samples; sensor wiring and calibration remain to be verified. |
| Left-motor current sense | Current-sensor analog output | **TBD** (was `A5 / PC0 / ADC1_IN10`) | Displaced by planned `IN2`; choose a new ADC-capable pin before rewiring. |
| Servo current sense | Current-sensor analog output | `A1 / PA1 / ADC1_IN1` | Planned ADC target; displaced from A4 by planned `IN3`. ADC firmware setup remains a later remap task. |
| Pi-to-STM32 serial | Pi `TX` → STM32 `RX` | `D0 / PA3 / USART2_RX` | Planned 115200-baud UART link. This takes over a current console pin; reassign or disable the Nucleo UART console in the firmware remap. |
| STM32-to-Pi serial | Pi `RX` ← STM32 `TX` | `D1 / PA2 / USART2_TX` | Planned 115200-baud UART link. This takes over a current console pin; reassign or disable the Nucleo UART console in the firmware remap. |
| Pi ground | Pi `GND` | Nucleo `GND` | Required for the UART link. |

### Current firmware allocation (before remap)

The following notes describe the compiled firmware's earlier pin assignment;
they are not the target wiring map above.

Pins intentionally unavailable for new wiring: SPI1 is disabled because D10,
D11, and D12 are motor/encoder pins; I2C1 is disabled because D15 and D14 are
blinker/encoder pins; I2C3 is disabled because D7 is a blinker pin.  Keep
`D0/PA3` and `D1/PA2` are the current board console UART pins. In the planned
map above they are reassigned to the Pi link, so the console must be moved or
disabled when that firmware remap is implemented.

`motor_control_init()` configures those pins and immediately calls
`motor_control_safe_stop()`, which sets ENA/ENB to 0% and IN1--IN4 low.
`IN4` was moved from `A3/PB0` to Arduino-header `D10/PB6` so PB0
remains available for the planned Part 3.5 current-sensor ADC input. SPI1 is
disabled because its default pins overlap D10 and the D11/D12 encoder inputs.

The Pi link keeps D2/PA10 for USART1 RX and D8/PA9 for USART1 TX. To avoid
that conflict, the front-left blinker uses D15/PB8 rather than D2. D7/PA8 and
D15/PB8 are blinker GPIOs, so I2C1 and I2C3 are disabled. The steering PWM is
D13/PA5 (TIM2_CH1), leaving encoder left-A on D3/PB3 available.

For a wheels-off-the-table wiring check, the Nucleo console exposes only the
bounded command `motor_pulse <a|b>`. It currently applies 100% PWM to the
corresponding L298N channel for 3000 ms, then automatically returns to the safe state. It is
a bench test, not the final throttle controller.

Bench result: L298N channel A (`OUT1/OUT2`) drives the physical right wheel
forward with `IN1=1, IN2=0`. Channel B (`OUT3/OUT4`) drives the physical left
wheel backward with `IN3=1, IN4=0`; the motor-control code therefore uses
`IN3=0, IN4=1` as its recorded forward polarity for channel B.

Final motor bench verification: with the original output-pair arrangement,
`motor_pulse a` moves the right wheel forward and `motor_pulse b` moves the
left wheel forward. Both tests use 100% PWM for 3000 ms with the wheels raised
and return automatically to the safe state.

### Encoder diagnostic result and current blocker

The intended inputs are right A=`D12`, right B=`D14`, left A=`D3`, and left
B=`D11`. `encoder_edges` is a read-only diagnostic that reports raw GPIO
interrupts separately from the signed quadrature count.

`encoder_monitor` continuously prints sampled A/B logic levels, while
`encoder_count_monitor` continuously prints signed quadrature counts and the
four raw A/B edge counters every 100 ms. Both monitors run until Ctrl-C.

```text
encoder_zero
encoder_edges            # baseline: all values should be zero
# manually rotate one wheel by hand
encoder_edges
encoder_status
```

Initial hand-turning tests with the harnesses connected produced zero raw
edges and zero signed counts. The left D3 and D11 inputs were independently
verified by momentarily grounding their signal paths through the shared
ground rail:

```text
left A / D3 grounded  -> raw left A=54
left B / D11 grounded -> raw left B=10
```

These values are intentionally not wheel measurements; the multiple edges
include jumper-contact bounce. With both right motor encoder signals detached
from the white connector but still attached to D12/D14, the firmware's pull-ups
gave `A=1, B=1`. After `encoder_zero`, grounding/releasing the two inputs in
the sequence `11 -> 01 -> 00 -> 10 -> 11` yielded `right=-4` from
`encoder_status`. This confirms that the right GPIO inputs, interrupts, and
signed quadrature transition table can decode a known-good sequence.

The earlier motor-driven diagnostic labels were captured before the physical
encoder sides were corrected in the devicetree, so they must not be used to
identify a wheel. Re-run `encoder_zero`, `encoder_edges`, and
`encoder_count_monitor` after flashing this mapping: `motor_pulse a` should
now be evaluated as the right D12/D14 encoder and `motor_pulse b` as the left
D3/D11 encoder. Raw edge counts alone do **not** establish valid quadrature or
wheel speed.

Next check: with the wheel raised, have a TA inspect both A and B at the
motor-side white connector with a scope/logic analyzer during one bounded
motor pulse. Compare the waveform there with the corresponding Nucleo input
to distinguish sensor output from jumper/connector problems. Measure each
channel relative to encoder ground, verify encoder VCC at the six-pin motor
connector, and confirm the encoder's rated supply voltage before changing the
3.3 V supply. Do not enable
closed-loop speed control until both wheel encoders produce repeatable signed
counts. Disconnect USB and 12 V before changing connector wiring.

## Revision history

- Initial Part 2 implementation and end-to-end bring-up.
- Part 3.1 motor bench pulse verified; encoder raw-edge diagnostic added and
  hardware connector blocker documented.
- Controlled right A/B sequence decoded to -4; motor-driven raw edges remain
  inconsistent with usable signed wheel counts, pending waveform inspection.
- Moved L298N IN4 from A3/PB0 to D10/PB6, disabled overlapping SPI1 pin use,
  and added continuous encoder level/count shell monitors with Ctrl-C exit.
- Integrated steering, blinkers, raw ADC status reporting, open-loop throttle,
  dynamic-brake paths, and a PID controller guarded by `pid_enable <0|1>`.
