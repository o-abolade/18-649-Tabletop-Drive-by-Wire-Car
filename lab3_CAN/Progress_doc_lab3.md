# 18-449/649 Lab 3 — Dual-Redundant CAN and Real-Time Analysis

Released 2026-10-04, **due 2026-10-22 11:59pm**.

Lab 2's single STM32 is split into three zones on two independent 500 kbit/s
CAN buses (A and B). Each node has one XIAO CAN module (MCP2515 + SN65HVD230)
per bus.

```
Logitech wheel --USB--> Laptop proxy --UDP:8000 (20ms)--> Raspberry Pi 4  (N1 cockpit)
                                                              |  can0 = A, can1 = B
  CAN A  [120R]---+----------------+----------------+---[120R]
  CAN B  [120R]---+----------------+----------------+---[120R]
                  |                |                |
                N1 Pi        N2 STM32 #1       N3 STM32 #2
               cockpit      steering/front    drivetrain/rear
```

## Repo structure

```
lab3_CAN/
├── pi_cockpit/        # N1: UDP -> CTRL/BLINK/HB_N1 on can0+can1, reads HB/FORCE
├── stm32_steering/    # N2: servo, front blinkers, RELAY, FORCE_TX, TEST_BTN
└── stm32_drivetrain/  # N3: motor PWM/DIR, PID, rear blinkers, TEST_BTN
```

All three currently start as copies of the Lab 2 code
(`../lab2_sensors_actuators/`). The UART link is to be replaced by CAN and each
zone trimmed to its own actuators.

## Message catalog (handout template)

Standard 11-bit IDs, DLC 8, every frame sent on **both** buses. Byte 0 =
4-bit sequence counter (low nibble) + 4 flag bits; receivers drop the
duplicate copy by `(ID, seq)`.

| ID | Name | From → To | Period | Deadline |
|---|---|---|---|---|
| 0x010 | CTRL | N1 → N2, N3 | every UDP update (20ms) | 2ms |
| 0x020 | FAULT | any → all | event, min 20ms | 100ms |
| 0x100 | HB_N1 | N1 → N2, N3 | 20ms | 20ms |
| 0x101 | HB_N2 | N2 → N1, N3 | 20ms | 20ms |
| 0x102 | HB_N3 | N3 → N1, N2 | 20ms | 20ms |
| 0x200 | BLINK | N1 → N2, N3 | on change + 500ms toggle, min 20ms | 100ms start; N2/N3 within 1ms |
| 0x300 | FORCE | N2 → N1 | 50ms | 50ms (dummy counter) |

**Team decisions still needed:** CTRL byte layout and throttle/brake u8 scale,
CRC-8 variant, sequence counter per-ID vs shared, flag bits, FAULT reason codes,
relay design (always-on vs on-degradation, loop prevention, RELAY priority).

## STM32 CAN pins (decided: handout wiring, both zones)

SPI1: SCK PA5 (D13), MISO PA6 (D12), MOSI PA7 (D11). CAN A: CS PB6 (D10),
INT PA10 (D2). CAN B: CS PC7 (D9), INT PB5 (D4).

These collide with Lab 2 pins, so each zone drops the functions it no longer
owns and moves the ones it keeps:

- **N2 steering:** servo PWM PA6 → **PB4 / D5** (still TIM3 CH1).
- **N3 drivetrain:** right ENB PA7 → **PB13** (still TIM1 CH1N), right IN3
  PB5 → **PA9 / D8**, encoder right B PA5 → **PA1 / A1**, encoder left B
  PB10 → **PB0 / A3** (PB10 shares EXTI10 with CAN A's INT on PA10),
  RL → **PB8 / D15**, RR → **PB9 / D14**.

Test-point, degraded-LED, ERR and test-button pins are still to be assigned.
The on-board user button B1 (PC13) can serve as TEST_BTN.

## Building

```bash
# from the zephyrproject workspace, one build dir per zone
west build -b nucleo_f401re <repo>/lab3_CAN/stm32_steering   -d <repo>/lab3_CAN/stm32_steering/build
west build -b nucleo_f401re <repo>/lab3_CAN/stm32_drivetrain -d <repo>/lab3_CAN/stm32_drivetrain/build
west flash -d <build dir> --dev-id <ST-Link serial>   # pick the right Nucleo when both are plugged in
```

Every new `add_library(...)` in `CMakeLists.txt` needs
`add_dependencies(<target> zephyr_generated_headers)` (pristine-build race).

Pi (`/boot/firmware/config.txt`, then reboot). The XIAO CAN module's MCP2515
crystal is **16 MHz** (marked `16.000MHz` in the Seeed wiki board photo), so
`oscillator=16000000` here and `osc-freq = <16000000>` in the Zephyr overlays:

```
dtparam=spi=on
dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25
dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=24
```

```bash
sudo apt install can-utils
bash pi_cockpit/can_up.sh     # sets 500k + restart-ms 100 on can0/can1, prints state
candump -c -t d any
```

### Part 0 bring-up app (`can_bringup/`)

Standalone Zephyr app with only the dual-MCP2515 overlay. The same build goes on
both Nucleos. **Don't connect any bus wire yet.** Each node must come up alone,
so leave every module's CANH/CANL screw terminal empty.

#### Nucleo wiring (identical on both boards)

The XIAO module's pins use **XIAO** names (D6–D10), not Nucleo Arduino names.
Module "D10" is MOSI, while Nucleo "D10" is CAN A's CS. Wire by function.

**The handout's module figure has MISO and MOSI swapped.** It labels D9 as MOSI
and D10 as MISO. On the real module (standard XIAO pinout) **D9 is MISO** and
**D10 is MOSI**. Confirmed on hardware 2026-10-19: following the handout, every
SPI read returned zeros; swapping the two wires made the probe and loopback pass.

Unplug any Lab 2 wiring on PA5, PA6, PA7, PB6, PC7, PA10 or PB5 first (encoder,
servo, motor enable, blinkers, Pi UART, motor IN3), or two outputs will fight.

| XIAO module pin | Function | CAN A module → Nucleo | CAN B module → Nucleo |
|---|---|---|---|
| 3V3 | power | 3V3 (power header) | 3V3 |
| GND | ground | GND | GND |
| D8 | SCK | D13 (PA5) | D13 (shared) |
| D9 | MISO | D12 (PA6) | D12 (shared) |
| D10 | MOSI | D11 (PA7) | D11 (shared) |
| D7 | CS | D10 (PB6) | D9 (PC7) |
| D6 (left header) | INT | D2 (PA10) | D4 (PB5) |
| 5V | — | leave unconnected | leave unconnected |

**3.3V only.** Never connect the module's 5V pin. There is no level shifting.

#### Pi wiring

Disconnect the Lab 2 UART wires to the STM32 first. The Lab 2 test points on
GPIO18/GPIO27 don't conflict.

| XIAO module pin | Function | CAN A (`can0`) → Pi | CAN B (`can1`) → Pi |
|---|---|---|---|
| 3V3 | power | pin 1 | pin 17 |
| GND | ground | pin 20 | pin 25 |
| D8 | SCK | pin 23 (GPIO11) | pin 23 (shared) |
| D9 | MISO | pin 21 (GPIO9) | pin 21 (shared) |
| D10 | MOSI | pin 19 (GPIO10) | pin 19 (shared) |
| D7 | CS | pin 24 (CE0) | pin 26 (CE1) |
| D6 | INT | pin 22 (GPIO25) | pin 18 (GPIO24) |

After adding the `config.txt` lines and rebooting, `dmesg | grep -i mcp251`
should show one "successfully initialized" line per chip.

#### Build and flash

Build directories aren't in git, so build locally first. Flash with only one
Nucleo plugged in so you can't flash the wrong board.

```bash
west build -p always -b nucleo_f401re <repo>/lab3_CAN/can_bringup -d <repo>/lab3_CAN/can_bringup/build
west flash -d <repo>/lab3_CAN/can_bringup/build --dev-id <ST-Link serial>
```

Expected console output for each of CAN A and CAN B:

```
CAN A: SPI probe CANSTAT=0x80 CANCTRL=0x87 -> OK (configuration mode)
CAN A: core clock 8000000 Hz (expect 8000000 = 16 MHz osc / 2)
CAN A: loopback OK (id=0x7ab dlc=8)
CAN A: ERROR-ACTIVE  TEC=0 REC=0
```

| Output | Means |
|---|---|
| SPI probe reads all zeros | Chip isn't answering: 3V3/GND, SCK, MISO (module **D9** → Nucleo D12; MOSI module **D10** → D11; the handout figure has these backwards), or CS |
| SPI probe reads all `0xFF` | MISO floating high, or CS not reaching the module |
| Probe OK, `loopback frame NOT received` | SPI works but INT isn't reaching the STM32 |
| `Timeout trying to set MCP2515 operation mode` | Same as all zeros: the driver can't read the mode back |

The Zephyr MCP2515 driver's init only **writes** to the chip, so "device ready"
doesn't prove the module is connected. The SPI probe is the real check.

## Status

| Part | Item | Status |
|---|---|---|
| 0 | Read XIAO crystal frequency | Done: 16 MHz |
| 0 | CAN bring-up app (`can_bringup/`) on each Nucleo, controller active | Builds; not yet flashed |
| 0 | Pi can0/can1 ERROR-ACTIVE @ 500k (`pi_cockpit/can_up.sh`) | Script written; not yet run |
| 1 | Bus wiring, 60Ω termination check, common ground | Not started |
| 1 | candump sees both Nucleos on both buses; jumper-pull test | Not started |
| 2 | Task communication graph figure | Not started |
| 3 | Node RTA (N2, N3), message RTA per bus, end-to-end chains, Pi latency | Not started |
| 4 | Dual-bus + relay design and re-analysis | Not started |
| 5 | Zephyr implementation (dispatch/dedup, send helper, HB_MON, RELAY, error SM, watchdog) | Not started |
| 5 | Pi cockpit (CTRL/BLINK/HB_N1 TX, HB/FORCE RX, bad-input injection) | Not started |
| 5 | Test-point breakout boards per node | Not started |
| — | Report, fault-injection matrix, Lab 1 revision-history entry | Not started |

## Revision history

- 2026-10-08: Lab 3 folder created from Lab 2 code; progress doc rewritten for Lab 3.
