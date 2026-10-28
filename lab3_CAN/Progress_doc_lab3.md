# 18-449/649 Lab 3 — Dual-Redundant CAN and Real-Time Analysis

Released 2026-10-04, **due 2026-10-22 11:59pm**.

> **Where the code is:** the Lab 3 implementation (shared CAN layer, both
> STM32 zones, Pi cockpit) is on **`main`** (merged 2026-10-21). The Part 0/1
> bring-up app `can_bringup/` is a test tool, not part of the final system, so
> it was left off `main`: it's on branch **`lab3-part0-can-bringup`**
> (`git switch lab3-part0-can-bringup`) if a module ever needs re-checking.

Lab 2's single STM32 is split into three zones on two independent 500 kbit/s
CAN buses (A and B). Each node has one XIAO CAN module (MCP2515 + SN65HVD230)
per bus.

```
Logitech wheel --USB--> Laptop proxy --UDP:8000 (20ms)--> Raspberry Pi 4  (N1 cockpit)
                                                              |  bus A = SPI CE0, bus B = CE1
  CAN A  [120R]---+----------------+----------------+---[120R]
  CAN B  [120R]---+----------------+----------------+---[120R]
                  |                |                |
                N1 Pi        N2 STM32 #1       N3 STM32 #2
               cockpit      steering/front    drivetrain/rear
```

## TA feedback fixes (2026-10-27)

- **Pi power-on startup:** added a systemd service and installer in `pi_cockpit/`.
  On the Pi, run `bash lab3_CAN/pi_cockpit/install_service.sh` once; this builds
  the cockpit, installs it under `/opt/lab3-cockpit`, and enables/starts
  `lab3-cockpit.service`. It waits for both CAN interfaces and brings them up
  before starting the cockpit; systemd restarts it after a failure. MCP2515
  overlays must already be configured. Logs: `journalctl -u lab3-cockpit -f`.
  Re-run the installer after updating the code. Power-cycle verification on
  the Pi is still required; adding the files alone does not install the service.
- **Normal neutral:** with fresh CTRL and NORMAL state, zero throttle and a
  released brake now disable both motor enables (coast), rather than applying
  full dynamic braking. Brake commands and ERROR/LOCAL_FAIL still brake.
  Startup remains fail-safe until communication is healthy.
- **Hazards:** the Pi discards turn-button changes and clears the previous
  turn/self-cancel state during hazards. The STM32s discard BLINK updates
  outside NORMAL and clear cached turn state on state transitions. Recovery
  requires a new turn-button press; a held button must be released first.
- **53.8 ms error timing:** the reported probes were last **N3 CAN_RX_B**
  pulse → **N2 ERR rising**. This crosses nodes and is not N2's heartbeat-age
  measurement: N3's receive pulse does not timestamp what N2 received.
  Heartbeat expiry uses N2's most recent heartbeat from a given peer on either
  bus, not an arbitrary CAN_RX pulse. The timeout remains three 20 ms periods,
  checked every 10 ms; immediate FAULT handling is a separate path.
  Heartbeat-loss transitions now log the peer and latest heartbeat age in ms
  (`-1` means never received). For N2 isolation, scope **N2 CAN_RX_B → N2 ERR**
  after disconnecting its other bus, and correlate with the transition log.
  Even that interval can be under 60 ms if the final RX pulse was CTRL rather
  than a heartbeat; a CAN trace identifying the last heartbeat is needed to
  check heartbeat age. The 66–88 ms expectation cannot be applied to the
  cross-node probe interval. No artificial timeout delay has been added.
- **Hardware recheck:** power-cycle with no login; verify neutral → throttle →
  neutral → brake; enter hazards with a turn already active and press both
  turn buttons during hazards, then restore and verify lamps stay off until
  a fresh turn press. Retake timing measurements after reflashing both zones.
- **Local verification:** both STM32 zone firmware builds pass. Host checks
  using the actual DRIVE and Pi blinker functions pass for neutral/coast,
  throttle, brake priority, fault braking, recovery, and hazard input discard.
  All three Pi shell scripts pass Bash syntax checks. The full Linux cockpit
  build and service power-cycle test must run on the Pi.

## Repo structure

```
lab3_CAN/
├── common/
│   ├── can_proto.{h,c}    # message catalog, payload layouts, CRC-8, (ID,seq) dedup
│   │                      #   plain C: used by the STM32s AND the Pi
│   └── zephyr/
│       ├── dual_can.overlay   # both MCP2515s, CAN test points, DEG/ERR LEDs, watchdog
│       ├── can_link.{h,c}     # dual-bus RX with dedup, send on A+B, per-bus HB times
│       ├── zone_core.{h,c}    # HB_TX, HB_MON, error state machine, TEST_BTN, FAULT, blink
│       ├── zone_config.h      # every threshold and thread priority, in one place
│       └── lab3_common.cmake  # shared build setup for both zones
├── stm32_steering/    # N2: STEER, FORCE_TX, RELAY (+ zone_core)
├── stm32_drivetrain/  # N3: DRIVE, PID (+ zone_core)
└── pi_cockpit/        # N1: cockpit.c (UDP -> CTRL/BLINK/HB_N1 on can0+can1), can_up.sh
```

(`can_bringup/`, the Part 0/1 test app, is on branch `lab3-part0-can-bringup`.)

Both STM32 zones are trimmed to their own actuators and run on the shared CAN
layer. Both build without warnings and have passed every checkoff scenario on
hardware (see "Full-system results").

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

### Payload layouts (decided; defined in `common/can_proto.h`)

Byte 0 for every frame: low nibble = sequence counter (**one counter per ID per
sender**), high nibble = flags (`0x10` = relayed copy, set by N2). Unused bytes 0.

| ID | Bytes 1–7 |
|---|---|
| CTRL `0x010` | b1–2 steer int16 LE −1000..+1000 · b3 throttle 0–100 % · b4 brake 0–100 % · b5 buttons (bit0 L, bit1 R, bit2 self-test) · b6 CRC-8 over b0 (flags masked)..b5 |
| HB `0x10x` | b1 zone state (0 ERROR, 1 NORMAL, 2 LOCAL_FAIL) · b2 bus health seen by sender (bit0 A ok, bit1 B ok) |
| BLINK `0x200` | b1 state (0 off, 1 left, 2 right, 3 hazard) · b2 phase (1 lamps on) |
| FAULT `0x020` | b1 failing zone (1–3) · b2 reason (1 test button, 2 CTRL out of range, 3 heartbeat lost) |
| FORCE `0x300` | b1 int8 (dummy counter) |

- **CRC-8:** Zephyr's `crc8_ccitt()` algorithm (poly 0x07, seed 0xFF), copied
  verbatim into `can_proto.c` so the Pi runs the same code. Check value
  `crc8(0xFF, "123456789") = 0xFB`.
- **CTRL test vector** (seq 3, steer −250, throttle 40 %, brake 0, left button):
  `03 06 FF 28 00 01 5B 00`. A CTRL with throttle/brake > 100, |steer| > 1000
  or unknown button bits is *out of range*: the zone enters ERROR and sends FAULT.
- **Dedup:** a frame is accepted if its seq is 1..7 ahead of the last accepted
  one for that ID, or if nothing arrived for 100 ms (sender reboot). This drops
  the second bus's copy, the relayed copy, and stale frames released from a TX
  buffer after a jumper is re-inserted.
- **Relay:** N2 **always** forwards, so there's no 60 ms detection delay in
  scenario (c). It forwards every first copy it didn't send itself (CTRL, BLINK,
  HB_N1, HB_N3, other zones' FAULT) onto the other bus with the relayed flag.
  No loops: only N2 relays, and it never forwards a flagged frame.

## STM32 CAN pins (decided: handout wiring, both zones)

SPI1: SCK PA5 (D13), MISO PA6 (D12), MOSI PA7 (D11). CAN A: CS PB6 (D10),
INT PA10 (D2). CAN B: CS PC7 (D9), INT PB5 (D4).

These collide with Lab 2 pins, so each zone drops the functions it no longer
owns and moves the ones it keeps:

- **N2 steering:** servo PWM PA6 → **PB4 / D5** (still TIM3 CH1).
- **N3 drivetrain:** right ENB PA7 → **PB13** (still TIM1 CH1N), right IN3
  PB5 → **PA9 / D8**, encoder right B PA5 → **PA1 / A1**, encoder left B
  PB10 → **PB0 / A3** (PB10 shares EXTI10 with CAN A's INT on PA10),
  RL → **PB9 / D14**, RR → **PB8 / D15** (left = D14, right = D15 on both zones).

**Test points and status LEDs, same pins on both STM32s** (morpho headers):

| Signal | Pin | | Signal | Pin |
|---|---|---|---|---|
| CAN_RX_A | PC8 | | DEG_A (LED) | PC3 (CN7-37) |
| CAN_RX_B | PC9 | | DEG_B (LED) | PC2 (CN7-35) |
| CAN_TX_A | PC10 | | ERR (LED) | PC12 |
| CAN_TX_B | PC11 | | TEST_BTN | on-board B1 (PC13) |

Zone-specific: N2 SRV PB4, FL PB9, FR PB8, RELAY_TX PB7. N3 PWM_SET PC6,
PWM_OUT PA0, DIR_A PB3 (IN4), RL PB9 (D14), RR PB8 (D15).

The board's default devicetree enables USART1 on PB6/PB7, I2C1 on PB8/PB9, I2C3
on PA8/PC9, SPI2 on PB12–15, the ADC on PA0 and a PWM on PA5, all on pins we
use. The shared overlay disables them (`can_bringup` only worked by luck).

### Threads and priorities (`common/zephyr/zone_config.h`)

| Priority | Task | Notes |
|---|---|---|
| coop 2 | MCP2515 RX ×2 (driver) | SPI read, dedup, dispatch; outranks all below |
| 0 | CAN_TX ×2 (one per bus) | per-bus queue ordered by CAN ID → the MCP2515's single TX buffer (see below) |
| 1 | RELAY (N2) | on the relayed brake path |
| 2 | STEER (N2) / DRIVE (N3) | CTRL → actuator |
| 3 | BLINK_F / BLINK_R | BLINK → lamps; local hazard while not NORMAL |
| 4 | TEST_BTN | single press = LOCAL_FAIL now (R2.5: ≤10 ms), double = restore |
| 5 | PID (N3) | 10 ms, per-wheel trim on top of DRIVE's feed-forward |
| 6 | HB_MON | 10 ms poll: per-bus HB timeout 60 ms → DEG LED; zone lost on both → ERROR; feeds watchdog |
| 7 | HB_TX | 20 ms absolute schedule; silent while LOCAL_FAIL |
| 8 | FORCE_TX (N2) | 50 ms |
| lowest | `main` status print | 1 s; dropped from Zephyr's default 0 after init (measured 1.2 ms per print) |

**The MCP2515 driver uses one TX buffer, not three.** Zephyr's driver hardcodes
`MCP2515_TX_CNT 1` (`drivers/can/can_mcp2515.h`), so each controller holds one
pending frame and can't arbitrate by ID among its own buffers. First hardware
run: N2 dropped ~10 frames/s per bus (every other FORCE, whenever it coincided
with HB_TX). Fix: `can_link` queues frames per bus **sorted by CAN ID** and a
per-bus TX thread feeds the single buffer, so a relayed CTRL overtakes queued
HB/FORCE. Result: 0 drops. **For Part 3:** a node's own lower-priority frame
already in the buffer can delay its next frame by up to one frame time
(270 µs), an extra blocking term on top of bus blocking. The Pi's `mcp251x`
driver likewise sends one frame at a time from a FIFO qdisc.

**Error state machine** (shared): power up in ERROR; NORMAL once both other
zones' heartbeats are seen on ≥1 bus and a valid CTRL arrived within 100 ms;
back to ERROR on a zone lost on both buses, a FAULT from another zone (held
until that zone sends a newer heartbeat), out-of-range CTRL (also sends FAULT)
or CTRL stale. Test button → LOCAL_FAIL (stops HB, sends FAULT). Not NORMAL =
ERR LED on, local hazard flashing, N3 braked. N2 keeps tracking the wheel in
ERROR (R3) and holds its angle while it is itself LOCAL_FAIL or isolated (R4.7).
The cockpit stops its heartbeat while LOCAL_FAIL or after 100 ms without wheel UDP.

### Building the zones

```bash
west build -b nucleo_f401re <repo>/lab3_CAN/stm32_steering   -d <repo>/lab3_CAN/stm32_steering/build
west build -b nucleo_f401re <repo>/lab3_CAN/stm32_drivetrain -d <repo>/lab3_CAN/stm32_drivetrain/build
```

Each prints one status line per second:
`N2 NORMAL | A ACTIVE T0 R0 | B ACTIVE T0 R0 | HB N1:AB N3:AB | CTRL age 4ms ...`
(`N1:AB` = N1's heartbeat heard on bus A and bus B; `-` = missing on that bus.)

### Pi cockpit (`pi_cockpit/cockpit.c`)

N1, in C with SocketCAN. It compiles the same `common/can_proto.c` as the STM32s.

| Task | What it does |
|---|---|
| UDP_RX → CTRL_TX | every wheel UDP packet → one CTRL on can0 and can1 (R4.4). No UDP = no CTRL, so the STM32s drop to ERROR after 100 ms |
| BLINK_TX | blinker master: left/right buttons toggle, self-cancel past ±400 and back (Lab 2 rule), lamps ON in the first frame, toggle every 500 ms (1 Hz); hazard 2 Hz while N1 isn't NORMAL |
| HB_TX | HB_N1 every 20 ms (absolute schedule) on both buses; silent while LOCAL_FAIL |
| HB_MON | N2/N3 heartbeats per bus → DEG_A/DEG_B; N1 is ERROR if N2 or N3 is lost on both buses, a FAULT arrived, or no wheel UDP for 100 ms |
| wheel test button | button 6: single press → LOCAL_FAIL + FAULT, double press → restore |
| FORCE_RX | prints N2's dummy force counter |

**`can0`/`can1` names are not stable.** Linux numbers them in whatever order
the two MCP2515s finish probing, and they swapped between boots (observed
2026-10-20: `can0` on `spi0.1`). The cockpit and `can_up.sh` identify each bus
by SPI chip select instead: **`spi0.0` (CE0) = bus A, `spi0.1` (CE1) = bus B**.
`can_up.sh` prints the mapping. When using `candump`/`cansend` by hand, check
which `canN` is which first.

Test points (BCM GPIO): UDP_RX 18 (pin 12), CMD_TX 27 (pin 13), CAN_TX_A 22
(pin 15), CAN_TX_B 23 (pin 16), DEG_A 6 (pin 31), DEG_B 5 (pin 29). UDP_RX goes
high on reception and low after both CTRL copies are written, followed by a
CMD_TX pulse, so UDP_RX's rising edge to CMD_TX is the Pi stage of the brake chain.

```bash
cd lab3_CAN/pi_cockpit
make                     # needs libgpiod v2 (same as Lab 2's pi_bridge)
./can_up.sh              # after every reboot
sudo ./cockpit           # sudo: TX watchdog can reset a stuck interface; add --rt for Part 3 latency runs
```

While it runs, type `b` + Enter to send **one** out-of-range CTRL (throttle 150)
for the bad-input demo, `s` to toggle the status line, `q` to quit.

### First full-system test (when bus B and the LEDs are wired)

1. **Cold start:** both Nucleos flashed with their zone apps, Pi `cockpit`
   running, proxy **stopped**. Expect ERR LED on and hazards on all three, no DEG
   LEDs, Pi status `N1 ERROR ... (no wheel UDP)`.
2. **Start the proxy:** within about 100 ms all three go NORMAL; N2/N3 status
   shows `HB N1:AB N3:AB` (resp. `N2:AB`) and `CTRL age` under 20 ms.
3. **Wheel:** SRV follows the wheel; pedal → PWM_SET/PWM_OUT on N3; brake →
   DIR_A. Left/right buttons → FL+RL (or FR+RR) blink together.
4. **Pull a jumper** (e.g. N2's bus A): that node's DEG_A lights within ~60 ms,
   everything keeps working; re-insert → off.
5. **Scenario (c):** pull N1's bus B and N3's bus A: DEG_B on N1, DEG_A on N3,
   RELAY_TX toggling on N2, wheel still steers, blinks and brakes.
6. **Test button:** B1 on N2 → N2 LOCAL_FAIL, N1/N3 ERROR (hazards, N3 braked);
   double press → all NORMAL.
7. **Bad input:** `b` in the cockpit → N2 and N3 ERROR and each sends FAULT.

### Full-system results (2026-10-21, functional pass)

| Check | Result |
|---|---|
| Cold start, proxy stopped | ✅ all three ERROR; zones hear each other's HB on A and B; Pi DEG LEDs off |
| Start proxy | ✅ all three NORMAL; Pi `UDP 50/s`, `tx A/B 100/s` (50 CTRL + 50 HB), 0 failures |
| Throttle / brake | ✅ N3 `cmd thr 53%` → `DRIVE`; pedal at rest → `BRAKE` |
| **Brake chain timing (R2.2, ≤2 ms)** | ✅ **max 940 µs over 15 captures** (first capture 645 µs). Scope: CH1 = N3 DIR_A (trigger, rising, Normal + Single), CH2 = Pi UDP_RX; driving, then brake pressed; Delay from the last UDP_RX rising edge to DIR_A rising. Single bus healthy (both buses connected). Spread = Pi (Linux) jitter. The chain starts at UDP_RX, so laptop ↔ Pi network delay isn't included |
| **Throttle chain timing (R2.1, ≤2 ms)** | ✅ **max 626 µs over 10 captures** (photo: 614 µs). Scope: CH1 = N3 PWM_SET first pulse (trigger rising), CH2 = Pi UDP_RX; braked, then throttle pressed. PWM_SET is a 10 µs pulse: use 100–200 µs/div or the scope won't sample it |
| **Relayed brake chain, scenario (c) (R2.2, ≤2 ms)** | ✅ **max 1.1 ms over 10 captures** (photo: 970 µs). Pi off bus B, N3 off bus A, so CTRL reaches N3 only via N2's RELAY. Same probes as the single-bus brake chain. Meets 2 ms even with the extra hop and second bus traversal |
| **Relay stage, scenario (c)** | Pi UDP_RX → N2 RELAY_TX (CTRL queued for bus B): **max 607 µs over 10 captures** (photo: 474 µs). With entry 3 (1.1 ms total) that leaves roughly 0.5 ms for the bus B hop + N3 receive + DRIVE. Stage maxima come from different captures, so their sum is an upper estimate, not one measured event |
| **Blinker sync (R2.4, ≤1 ms)** | ✅ **max 768 µs over 10 edges** (photo: 186 µs), N2 FL vs N3 RL. Passes, but with less margin than expected. Likely causes for the report: (1) each node switches on whichever copy of BLINK it accepts first; the Pi writes A then B, so if one node is briefly busy (SPI read of another frame) it can act on the later bus's copy, a frame time or more after the other node; (2) the lamp thread (prio 3) is below CAN_TX/RELAY/STEER, and N2 relays everything. Possible improvement: switch the lamps directly in the RX dispatch callback (removes (2), not (1)) |
| **Pi stage, UDP_RX → CMD_TX** | 3000 samples per run (60 s), scope Delay statistics. **Without `--rt`:** max 442.5 µs (mean 53 µs) normal, max 444.5 µs (mean 104 µs) under `stress`; the mean doubles under load, the max comes from kernel activity. **With `sudo ./cockpit --rt` (pinned to CPU 3, SCHED_FIFO 80):** max **126.5 µs** (mean 34.9 µs) normal, max **82 µs** (mean 32.1 µs) under `stress`. SCHED_FIFO cuts the worst case ~3.5× and the mean no longer grows under load, so **the cockpit runs with `--rt`** (handout: "say so in the report"). **Use 126.5 µs (larger of the two `--rt` maxima) as the Pi term.** The stressed max being lower is sampling variation over one minute. Photos 1–5 (chains) were taken without `--rt`, so they are conservative. No 99th percentile: the scope gives max/mean only |
| Steering | ✅ N2 `cmd steer` tracks the wheel (−604..+35 seen). Servo not connected; SRV = PB4 on the scope |
| Turn signals | buttons reach N2/N3 (`btn 0x02`); lamp sync to be confirmed on the scope (FL vs RL) |
| Test button (N2 B1) | ✅ N2 LOCAL_FAIL + FAULT; N3 `NORMAL -> ERROR (FAULT from N2)`, `N2:--`; double press → all NORMAL |
| Bad input (`b`) | ✅ N2 and N3 each sent FAULT reason 2 (CTRL out of range); N1 `ERROR (FAULT)`; all back to NORMAL on the next valid CTRL (~20 ms) |
| Pi bus A pull | ✅ Pi DEG_A on, its `tx A` fails; re-insert → `tx A` back to normal within 1 s, no reset |
| Scenario (c): Pi off B, N3 off A | ✅ N3 `HB N1:-B N2:-B`, NORMAL, **`thr 50%` → `DRIVE` via N2's relay**; N2 `N1:A- N3:-B`, relay ~150 frames/s, 0 dropped; DEG_B Pi, DEG_A N3. Re-insert → all `N?:AB`, ACTIVE, no reset |
| Isolation (d): all 4 of N2's bus wires | ✅ N2 ERROR, both buses PASSIVE/DEG, `N1:-- N3:--`; N3 ERROR (`N2:--`), motors braked, its buses ACTIVE (no DEG); re-insert → all NORMAL, no reset. Recovery time not yet measured with the scope |
| Whole-bus loss (e): bus A off at every node | ✅ (after the Pi TX watchdog fix below) all DEG_A on, all NORMAL on bus B only (`N?:-B`), CTRL gaps ~36–39 ms; re-insert → all `N?:AB`, ACTIVE |

**Problems found and fixed on the way:**
- **Zephyr's MCP2515 driver uses one TX buffer** → frames dropped; fixed with the per-bus ID-ordered TX queue (see Threads and priorities).
- **Pi `can0`/`can1` swap between boots** → buses now identified by chip select.
- **Proxy sent at 31/s, not 50/s:** Qt's default timer on Windows rounds 20 ms up to 31.25 ms. Fixed in `proxy_gui.py` with `setTimerType(QtCore.Qt.PreciseTimer)` on both timers and `TX_INTERVAL_MS = 20`.
- **Brief NORMAL → ERROR blips, `CTRL stale, age ~105 ms`:** wheel UDP delayed by Wi-Fi (max gaps 85–139 ms; Pi Wi-Fi power save made it worse, now turned off by `can_up.sh`). **Use an Ethernet cable laptop ↔ Pi for the checkoff.**
- Pulling a jumper mid-frame causes up to ~140 ms of CTRL disruption on the other nodes while both buses retransmit; steady state afterwards is normal (~40 ms max gap).
- With no motors/encoders connected, N3's PID trim saturates, so PWM duty sits at 100 % while driving. Expected on a bare board.
- **Pi TX hang (twice):** the `mcp251x` driver's TX queue stopped draining for good ("No buffer space available") while RX on the same interface still worked: `can1` after re-flashing the Nucleos, then `can0` during the whole-bus-A test, which cut the Pi off from every node. `can0` stats: error-passive 18×, bus-off 0×, so it follows repeated no-ACK episodes, not bus-off. **Fix: the cockpit's TX watchdog** resets an interface whose writes have all failed for 200 ms while N2/N3 are still heard on it (at most once per second). **Run the cockpit with `sudo ./cockpit`** so it can do the reset; without root it only prints a warning.
- Terminal output can stall the cockpit (one ~6 s freeze while copying text); turn the status line off with `s` during timing runs.

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

> This app lives on branch `lab3-part0-can-bringup`, not `main`. Switch to
> that branch to build or flash it; the commands below assume you have.

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

### Part 1: wiring the dual bus

**Bus order and termination.** Same order on both buses: **N1 (Pi) at one end,
N2 (steering) in the middle, N3 (drivetrain) at the other end.** With no discrete
resistors, short **P1** on N1's two modules and N3's two modules; leave N2's open.
Each bus then has one 120 Ω at each end. Pulling an N1 or N3 jumper removes that
end's terminator (the P1 terminator sits behind the jumper). That's a known
trade-off of using P1.

1. **Common ground first:** one GND wire between the Pi, both Nucleos and the
   bus breadboard, before anything else.
2. **Four rails** on the bus breadboard: `A_H`, `A_L`, `B_H`, `B_L`. One wire
   color per bus, buses on different rails.
3. **Each module:** screw-terminal CANH → its bus's `_H` rail, CANL → `_L` rail,
   with one jumper each (these jumpers are the R4.8 fault-injection points).
   N1's and N3's taps go at the two ends of each rail, N2's in between.
4. **Power everything off and measure CANH–CANL on each bus: 60 Ω.**
   120 Ω = one terminator missing, 40 Ω = an extra P1 is shorted.
5. Keep the bus wires short, and twist each H/L pair if you can.

### Part 1: flashing with a node number

Each Nucleo sends test frame `0x700 | (node << 4)` every 100ms on **both**
buses, so N2 sends `0x720` and N3 sends `0x730`. Byte 0 is the node number,
byte 1 is the bus letter (`0x41` 'A' / `0x42` 'B'), and bytes 2–5 are a counter,
the same on both buses. Build with the board's node number:

```bash
west build -p always -b nucleo_f401re <repo>/lab3_CAN/can_bringup -d <repo>/lab3_CAN/can_bringup/build/n2 -- -DCONFIG_BRINGUP_NODE_ID=2
west flash -d <repo>/lab3_CAN/can_bringup/build/n2
# other board: n3 / NODE_ID=3
```

Once a second each Nucleo prints one line per bus:

```
CAN A: ERROR-ACTIVE  TEC=  0 REC=  0 | tx ok 10 err  0 full  0 | rx/s N1 10 N2  0 N3 10 other 0
```

`tx ok` should be about 10/s. `rx/s` counts frames heard from each node.
A node alone on a bus gets no ACK. The MCP2515 retries the frame
automatically forever, so `tx err` stays **0**. Instead `tx ok` drops to 0,
the three TX buffers fill, and `full` climbs to about 10/s. `TEC` jumps by 8
per failed attempt to **128** and stops there (an error-passive node doesn't
count ACK errors), and the state is ERROR-PASSIVE. That's expected until a
second node is connected.
`-DCONFIG_BRINGUP_TX_PERIOD_MS=0` turns sending off.

### Part 1: Pi side

```bash
candump -c -t d any        # every frame on both buses, with time between frames
```

Make the Pi act as N1 (`0x710`, 10 Hz on both buses) in a second terminal:

```bash
i=0; while true; do c=$(printf '%02X' $((i & 0xFF)))
  cansend can0 710#0141${c}0000000000; cansend can1 710#0142${c}0000000000
  i=$((i+1)); sleep 0.1; done
```

### Part 1 checkpoint (handout)

1. **One node at a time:** with only N2 running, `candump` shows `0x720` on
   both `can0` and `can1` about every 100ms. Repeat with only N3 (`0x730`).
2. **All together:** both Nucleos plus the Pi sender. Each Nucleo's `rx/s`
   shows about 10 from each of the other two nodes, on both buses.
3. **Pull one node's bus A jumper** (pick N2, the unterminated middle node):
   - bus B traffic is unchanged everywhere;
   - bus A still carries `0x710` and `0x730` between N1 and N3;
   - N2's CAN A shows `tx ok 0`, `full` about 10/s, `rx/s` all 0 and
     ERROR-PASSIVE, and N2 disappears from `can0`. `tx err` stays 0 because
     the controller keeps retrying. Which error counter passes 127 depends on
     how the loose wires float: with no ACK `TEC` parks at 128, and with a
     noisy stub `REC` climbs instead and the node may never start a
     transmission. **Measured 2026-10-19: `TEC=0 REC=135`.**
   - Re-insert: N2's CAN A returns to ERROR-ACTIVE on its own with both counters
     back to 0. Any frames stuck in its TX buffers go out at once (a burst of
     old counter values on `candump`). **Measured: full recovery, no reset.**
   - **Design note:** those buffered frames arrive late. Harmless for test
     frames, but a stale CTRL/brake frame must be rejected by the receiver's
     sequence check, or aborted at the sender when its bus degrades.
4. **Scope CANH and CANL of each bus:** the idle (recessive) level, where CANH
   and CANL sit at about the same voltage, and one frame, where CANH rises
   and CANL falls in the dominant bits.

## Test-point breadboards

The handout wants **one small breadboard per node, one signal per row, in this
exact order**, labeled on tape beside the board; the TAs clip onto these boards
and nothing else. Run a jumper from each pin to its row and tie rows 1–2 to that
node's GND. Where a signal already drives an LED (DEG, ERR, blinkers), add a
second wire from the same pin to the row.

| Row | N1 (Pi) | N2 steering (front) | N3 drivetrain (rear) |
|---|---|---|---|
| 1 | GND | GND | GND |
| 2 | GND | GND | GND |
| 3 | UDP_RX: pin 12 (GPIO18) | CAN_RX_A: PC8 (CN10-2) | CAN_RX_A: PC8 (CN10-2) |
| 4 | CMD_TX: pin 13 (GPIO27) | CAN_RX_B: PC9 (CN10-1) | CAN_RX_B: PC9 (CN10-1) |
| 5 | CAN_TX_A: pin 15 (GPIO22) | CAN_TX_A: PC10 (CN7-1) | CAN_TX_A: PC10 (CN7-1) |
| 6 | CAN_TX_B: pin 16 (GPIO23) | CAN_TX_B: PC11 (CN7-2) | CAN_TX_B: PC11 (CN7-2) |
| 7 | DEG_A (LED): pin 31 (GPIO6) | DEG_A (LED): PC3 (CN7-37) | DEG_A (LED): PC3 (CN7-37) |
| 8 | DEG_B (LED): pin 29 (GPIO5) | DEG_B (LED): PC2 (CN7-35) | DEG_B (LED): PC2 (CN7-35) |
| 9 | — | SRV: PB4 (D5) | PWM_SET: PC6 (CN10-4) |
| 10 | — | FL: PB9 (D14) | PWM_OUT: PA0 (A0) |
| 11 | — | FR: PB8 (D15) | DIR_A: PB3 (D3), motor IN4 |
| 12 | — | RELAY_TX: PB7 (CN7-21) | RL: PB9 (D14) |
| 13 | — | ERR: PC12 (CN7-3) | RR: PB8 (D15) |

Notes:
- CN7/CN10 are the Nucleo's long morpho headers; double-check pin numbers
  against the silkscreen, since those rows are easy to be off by one on.
- **What each point means:** UDP_RX goes high when the Pi receives a wheel
  packet and low once both CTRL copies are written, followed by a CMD_TX pulse.
  CAN_TX_x rising = that frame's turn in the per-bus TX queue, falling = handed
  to the MCP2515. CAN_RX_x = the receive dispatch for a frame from that bus
  (held ≥10 µs). RELAY_TX pulses when N2 queues a forwarded frame. ERR is high
  while the zone is not NORMAL.
- **DIR_A only changes on a transition:** it rises when N3 goes from driving to
  braking. Hold the throttle first, then press the brake, and trigger on DIR_A
  rising (Normal + Single). From an already-braked state there is no edge.
- Timing runs: laptop ↔ Pi on **Ethernet**, cockpit as `sudo ./cockpit --rt`
  with the status line off (`s`), scope trigger mode **Normal**, and report the
  **maximum** of repeated captures.

## Scope capture log

Scope screenshots are phone photos, taken in this order: **photo N on the phone
= entry N here**.

**Files (2026-10-22):** the six measurements the TA clarification requires are in
`Doc/assets/lab3_scope/required/m1.jpeg` … `m6.jpeg` (M1 = photo 6, the Pi stage;
M2–M6 still to take). Every other capture is in `Doc/assets/lab3_scope/additional/`
with its photo number. Use the "Label" column as the figure caption in the report.
For every timing value, report the **maximum** of the repeated captures, not
the single photo's value.

| # | Label (figure caption) | Channels / trigger | Shown in photo | Max over repeats |
|---|---|---|---|---|
| 1 | Brake chain (R2.2): Pi UDP_RX → N3 DIR_A | CH1 = N3 DIR_A (row 11, trigger rising, Normal + Single); CH2 = Pi UDP_RX (row 3). Driving, then brake pressed | one capture: **640 µs** | **940 µs** over 15 captures |
| 2 | Throttle chain (R2.1): Pi UDP_RX → N3 PWM_SET (first pulse) | CH1 = N3 PWM_SET (row 9, trigger rising, Normal + Single, **100–200 µs/div** with the trigger point at the right; the 10 µs pulse is missed at 500 µs/div); CH2 = Pi UDP_RX (row 3). Braked, then throttle pressed | one capture: **614 µs** | **626 µs** over 10 captures |
| 3 | Relayed brake chain, scenario (c): Pi UDP_RX → N3 DIR_A, via N2 RELAY | Pi off bus B, N3 off bus A. CH1 = N3 DIR_A (row 11, trigger rising, Normal + Single, 500 µs/div); CH2 = Pi UDP_RX (row 3). Driving, then brake pressed | one capture: **970 µs** | **1.1 ms** over 10 captures |
| 4 | Relay stage, scenario (c): Pi UDP_RX → N2 RELAY_TX (CTRL forwarded) | Pi off bus B, N3 off bus A. Trigger = Pi UDP_RX (row 3, rising, Normal + Single, 100 µs/div, trigger point at left); other channel = N2 RELAY_TX (row 12 = PB7, CN7-21). First RELAY_TX pulse ≥ 0.2 ms after UDP_RX (earlier ones are forwarded heartbeats) | one capture: **474 µs** | **607 µs** over 10 captures |
| 5 | Blinker sync (R2.4): N2 FL vs N3 RL rising edges, left blinker | Both buses connected. CH1 = N2 FL (row 10, trigger rising), CH2 = N3 RL (row 12); Delay CH1 → CH2 rising with statistics, 10 blink edges | one capture: **186 µs** | **768 µs** over 10 edges (limit 1 ms) |
| 6 | Pi stage, normal load, **with `sudo ./cockpit --rt`** (CPU 3, SCHED_FIFO 80): Pi UDP_RX → Pi CMD_TX | CH1 = Pi UDP_RX (row 3, trigger rising, Normal), CH2 = Pi CMD_TX (row 4), ~50 µs/div; Delay CH1 → CH2 with statistics, 3000 samples (60 s) | statistics on screen | **126.5 µs** max, mean 34.88 µs, 3000 samples |
| 7 | Pi stage under `stress --cpu 4 --io 2`, **with `--rt`** | Same as 6, with the stress load running | statistics on screen | **82 µs** max, mean 32.11 µs, 3000 samples |
| — | *(no photo)* Pi stage, normal load, **without `--rt`** | Same as 6, normal scheduling | — | 442.5 µs max, mean 52.97 µs, 3000 samples |
| — | *(no photo)* Pi stage under `stress`, **without `--rt`** | Same as 7, normal scheduling | — | 444.5 µs max, mean 103.96 µs, 3000 samples |

Photos 3 and 4 were retaken on 2026-10-21 after bus B's CANL wiring fix: same maxima (1.1 ms, 607 µs). Photos 1–5 were taken without `--rt`, so the chain values include the slower
Pi stage (up to ~443 µs vs 126.5 µs): they are conservative.

### Remaining captures (planned, photos 8–17)

These fill the timing column of the report's fault-injection matrix, plus the
Part 1 and R2.5 checks. Setup:
- Proxy and `sudo ./cockpit --rt` running, everything NORMAL.
- Scope trigger **Normal + Single**, unless the row says otherwise.
- A "jumper pull" means **both** wires (CANH and CANL) for that bus.

The test button is the Nucleo's blue B1 (PC13, CN7-23; active low, so it
falls on press). It isn't on a breadboard row, so probe CN7-23 directly. N3's
ERR (PC12, CN7-3) isn't on N3's breadboard either; probe it directly.

| # | Label (figure caption) | Channels / trigger | How | Limit | Max over repeats |
|---|---|---|---|---|---|
| 8 | CAN A physical layer: CANH/CANL, idle and one frame | CH1 = CANH, CH2 = CANL on the bus A rail (probe ground at bus GND), Math CH1−CH2; trigger CH1 rising, ~20 µs/div | Recessive ≈ 2.5/2.5 V (0 V diff), dominant ≈ 3.5/1.5 V (≈ 2 V diff), 2 µs bits | Part 1 | **Taken.** Meas Top/Base: CANH idle 2.18 V, dominant 2.68 V; CANL idle 2.21 V, dominant 0.61 V. Differential ≈ 0 V idle, **2.07 V dominant** (ISO 11898-2: 1.5–3.0 V) ✅. Low idle level and asymmetric swing are typical of a 3.3 V transceiver |
| 9 | CAN B physical layer | Same on the bus B rail | | Part 1 | **Taken.** CANH idle 2.20 V, dominant 2.70 V; CANL idle 2.19 V, dominant 0.73 V. Differential ≈ 0 V idle, **1.97 V dominant** ✅. Found while setting up: bus B's CANL was miswired to ground (CANL read 0 V at every module). CAN kept working on CANH alone with poor margin, which caused bus B's occasional errors. Rewired 2026-10-21 |
| 10 | (a) DEG_A on: N2 off CAN A | Trigger = N2 DEG_A (row 7) rising, 20 ms/div; CH2 = N2 CAN_RX_A (row 3) | Pull N2's bus A jumpers. Delta = last CAN_RX_A pulse → DEG_A rising. 5 repeats | 60 ms of silence (+ ≤ 10 ms poll) | **Photo: 62.8 ms; max 68.6 ms over 5** ✅ (design 60–70 ms). Measured with X cursors: Delay would start from the first CH1 pulse on screen, not the last. Needs 20 ms/div + Acquire → Peak Detect so the 10 µs CAN_RX pulses show |
| 11 | (f) DEG_A off: N2 bus A restored | Trigger = N2 DEG_A falling; CH2 = N2 CAN_RX_A | Re-insert the jumpers. Delta = first CAN_RX_A pulse → DEG_A falling. 5 repeats | ≤ 100 ms | **Photo: 9.6 ms; max 14.8 ms over 5** ✅. DEG clears on the first heartbeat heard (not just any frame) at the next 10 ms poll: design bound ≤ 30 ms |
| — | (b) quick check, no photo | — | Pull N3's bus B jumpers: N3 DEG_B on, everyone stays NORMAL, wheel still drives; re-insert → off | functional | |
| 12 | R2.5 local fail: N2 B1 → N2 ERR | CH1 = N2 B1 (CN7-23), trigger falling, 2 ms/div; CH2 = N2 ERR (row 13) | Single press, then double press to restore. 5 repeats | ≤ 10 ms | **Photo: 20 µs; max 20 µs over 5** ✅ (Meas → Delay, CH1 falling → CH2 rising) |
| 13 | R2.5 system error: N2 B1 → N3 ERR | CH1 = N2 B1, trigger falling, 10 ms/div; CH2 = N3 ERR (PC12, CN7-3) | Single press (FAULT path). 5 repeats | ≤ 100 ms | **Photo: 1 ms; max 1.1 ms over 5** ✅ via N2's FAULT frame (Meas → Delay) |
| 14 | (d) isolation, others enter error: N2 off both buses → N3 ERR | CH1 = N2 CAN_RX_B (row 4); CH2 = N3 ERR, trigger rising, 20 ms/div | Pull N2's bus A jumpers first, then bus B. Delta = last N2 CAN_RX_B pulse (isolation complete) → N3 ERR rising. 3 repeats | ≤ 100 ms | **Photo: 59.4 ms; max 68.4 ms over 3** ✅ (X cursors, Peak Detect) |
| 15 | (d) isolation, isolated node's safe state: → N2 ERR | CH1 = N2 CAN_RX_B; CH2 = N2 ERR (row 13), trigger rising | Same pull. Delta = last CAN_RX_B pulse → N2 ERR rising | ≤ 100 ms | **Photo: 52.4 ms; max 53.6 ms over 3** ✅. Under 60 ms because the timeout runs from the last heartbeat heard, and the last CAN_RX pulse can be a later non-heartbeat frame (e.g. CTRL) |
| 16 | (f) isolation recovery, N2 | CH1 = N2 CAN_RX_A (row 3); CH2 = N2 ERR, trigger falling, 100 ms/div | Re-insert all of N2's jumpers. Delta = first CAN_RX pulse → N2 ERR falling | ≤ 500 ms, no reset | **Photo: 3.6 ms; max 15.6 ms over 3** ✅, no reset |
| 17 | (f) isolation recovery, N3 | CH1 = N2 CAN_RX_A; CH2 = N3 ERR, trigger falling | Same. Delta = first N2 CAN_RX pulse → N3 ERR falling | ≤ 500 ms | **Photo: 16.5 ms; max 16.5 ms over 3** ✅, no reset. Check CH2 in Auto first (flat ≈ 3.3 V while N3 is in ERROR): CN7-1 next to it is the toggling CAN_TX_A |

**No scope needed:**
- **Heartbeat cadence (20 ms ±10 %)**, on the Pi, for the interface
  `can_up.sh` reports as bus A. This prints min/max spacing per heartbeat ID
  over 20 s, skipping N2's relayed copies (flag nibble ≠ 0):
  ```
  timeout 20 candump -ta can0,100:7FF,101:7FF,102:7FF | awk '{t=substr($1,2,length($1)-2); id=$3; if (substr($5,1,1)!="0") next; if (id in last){d=(t-last[id])*1000; if(!(id in mn)||d<mn[id])mn[id]=d; if(d>mx[id])mx[id]=d; n[id]++} last[id]=t} END{for(i in n) printf "%s n=%d min %.2f ms max %.2f ms
", i, n[i], mn[i], mx[i]}'
  ```
- **Blinker rate:** the scope's Freq and +Duty measurements on N2 FL (row 10)
  with the left blinker on (expect 1 Hz ±10 %, 50 % duty) and on hazard in
  ERROR (expect 2 Hz). A screenshot is optional.


## WCET measurements (2026-10-21)

**Method:** `common/zephyr/wcet.{h,c}`. Each job reads the current thread's
execution cycles (Zephyr thread runtime stats, 84 MHz) at start and end. That
excludes preemption and includes ISR time (conservative). The max per task is
kept, and both zones print `WCET max us (jobs) | ...` every 5 s on the
console.
- **Run 1:** full system with the exercise run: throttle, brake, steer,
  blinkers, test button, bad input, bus pulls.
- **Run 2:** final firmware, normal driving.
- **Table values:** the max of both runs.

`DRV_FRAME` is the MCP2515 driver thread's run time between two of our
`rx_handler` calls on the same bus: the driver's own per-frame SPI/flag work.
- **Per-frame CAN cost:** `C_ev = DRV_FRAME + RX_DISPATCH`.
- **Why it's large:** one 3-byte SPI read takes **30 µs** (4.6 µs on the wire;
  boot print `CAN x: 3-byte SPI read took N us`), so per-transaction overhead
  in the polled STM32 SPI driver dominates.

| Task | N2 max µs (jobs) | N3 max µs (jobs) |
|---|---|---|
| RX_DISPATCH (incl. 10 µs TP) | 50.9 (95 367) | 50.1 (145 575) |
| DRV_FRAME (clean run only, see below) | 680.5 (96 007) | 452.9 (164 313) |
| CAN_TX | 155.8 (99 681) | 162.5 (357 784) |
| RELAY (incl. 10 µs TP) | 86.8 (468 206) | — |
| STEER / DRIVE | 37.8 (20 805) | 95.8 (8 056) |
| BLINK | 29.2 (204) | 39.8 (186) |
| TEST_BTN (accepted presses) | 65.7 (50) | 127.9 (3) |
| PID | — | 122.9 (33 527) |
| HB_MON | 120.1 (42 023) | 232.0 (357 785) |
| HB_TX | 64.6 (3 252) | 65.2 (178 892) |
| FORCE_TX | 64.7 (8 404) | — |
| STATE_SPI (sched-locked) | 118.5 (7 140) | 120.5 (7 150) |
| STATUS (main, now lowest prio) | 1332.6 (319) | 1228.7 (334) |
| MCP2515 thread peak run | 489.5 / 346.1 | 361.4 / 450.0 |

**Runs (2026-10-21):** each value is the max over all runs:
1. the exercise run;
2. final firmware, bus B still miswired;
3. after the bus B fix, through photos 10–17 (jumper pulls);
4. a clean ~5 min run (bus B fixed, no jumper pulls).

**DRV_FRAME uses only run 4.** With a node unplugged, the driver handles
error interrupts for seconds between two received frames, and all of it
lands in one "gap" (run 3 showed 254 ms on N2). The fixed bus B made
DRV_FRAME *higher* (N2 435 → 680 µs), not lower: more real traffic and
interrupts. The RTA charges C_ev once per **received** frame (N2 ≤ 12,
N3 ≤ 20 per 20 ms), because the TX-done work for a zone's own frames is
already inside DRV_FRAME.

**Firmware changes made from these numbers:**
- **The status print was the worst offender.** It ran for up to 1.2 ms at
  priority 0 (above STEER/DRIVE) once a second. Fixes:
  - `main` drops to `ZONE_PRIO_STATUS` (lowest) after init.
  - `can_link_bus_state()` does its SPI reads with the scheduler locked. The
    SPI lock is a `k_sem` with no inheritance, so a preempted low-priority
    holder would otherwise stall CAN_TX and the driver threads behind every
    middle-priority task.
  - N3 prints the drive state after releasing `motor_lock`.
  - `wcet_print` uses `k_thread_foreach_unlocked`, so interrupts aren't off
    during `printk`.

**RTA result (`Doc/lab3/lab3_report.md` §3):**
- **What dominates:** the per-frame driver cost (503 µs N3, 731 µs N2) is
  larger than the 222 µs minimum frame spacing, and two buses feed each
  zone. A worst-case burst of received frames (12 on N2, 20 on N3)
  therefore holds off every task for about 9–10 ms.
- **Fail by analysis:** brake/throttle 12.9 ms, relayed brake 24.8 ms,
  blinker sync 15.3 ms, local fail 11.4 / 11.3 ms (N2 / N3). N3's PID and
  HB_MON can also run up to 1.5 ms past their 10 ms period during a burst.
- **Meet as measured:** brake 940 µs, throttle 626 µs, relayed 1.1 ms, sync
  768 µs, N2 local fail 20 µs.
- **Meet by analysis:** steering, blinker start, both error chains (worst
  96.9 ms of 100) and every message deadline.
- **Remedies (not implemented):** listed at the end of §3.3. They are a
  leaner MCP2515 receive path / SPI DMA, relaying only on demand, and a
  priority qdisc on the Pi.

## Part 3 measurement checklist

`Doc/lab3/lab3_report.md` now contains the Section 3.1--3.3 RTA structure using
the handout WCET placeholders.  It is not final until the following values
replace or validate those assumptions.  Record the **maximum** observed value,
the capture conditions, and the test-point pair used; do not replace a bound
with an average.

| Measurement | How to capture it | Needed for |
|---|---|---|
| Pi `UDP_RX` → `CMD_TX` latency | Scope N1 test points for at least one minute with normal traffic, then again under a Pi stress load. Record maximum and 99th percentile. | `J_PI`; this is the limiting input to the 2 ms brake/throttle chain. The report's current 300 us value is an assumption only. |
| N2/N3 task WCETs | Toggle a spare GPIO around the non-preempted body of each task, then scope it. Measure CAN RX/dispatch, STEER, DRIVE, BLINK_F/BLINK_R, HB_TX, HB_MON, FORCE_TX, PID, and the test-button ISR. | Replace every placeholder `C` in the Node 2 and Node 3 RTAs. State how preemption was excluded or accounted for. |
| MCP2515 SPI transaction / lock hold time | Scope a GPIO around one RX or TX SPI transaction while both CAN modules are active. Measure the longest critical section. | Validate the 60 us shared-SPI blocking term `B`; use 120 us instead if the lock spans both-bus transactions. |
| CAN frame duration and local TX serialization | Scope CANH/CANL (or CAN_TX) for an 8-byte catalog frame and for a lower-priority frame already queued before CTRL. | Validate the 270 us frame/blocking assumption and document any one-buffer MCP2515 TX delay. |
| Brake/throttle chain | Trigger on N1 `UDP_RX`; probe N1 `CAN_TX_A`, N3 `CAN_RX_A`, and N3 `DIR_A`/`PWM_SET`. Take the maximum of repeated captures. | Compare the measured end-to-end result with the 2 ms analysis bound. |
| Steering chain | Trigger N1 `UDP_RX`; probe N2 `CAN_RX_A` and `SRV`. | Verify the 50 ms chain and separate Pi, bus, and N2 latency. |
| Blinker start and synchronization | Trigger on BLINK transmission; scope N2 `FL` and N3 `RL` together for at least ten toggle edges. | Verify ≤100 ms start and report the **maximum** front/rear edge offset against the 1 ms limit. |
| Error propagation | Press N2 or N3 TEST_BTN; scope its local safe-state output and the other zone's `ERR`/hazard output. | Verify local fail ≤10 ms and remote error/hazard/brake ≤100 ms, including the three-missed-heartbeat delay. |

When these are recorded, update the report tables so every value is labeled
**measured** (with method) or **assumed** (with reason).  Re-run the CAN
message and end-to-end calculations if any measured WCET, blocking time, or Pi
maximum is larger than the current placeholder.

## Status

| Part | Item | Status |
|---|---|---|
| 0 | Read XIAO crystal frequency | Done: 16 MHz |
| 0 | CAN bring-up app (`can_bringup/`) on each Nucleo, controller active | Board 1: CAN A + B pass. Board 2: CAN A passes, CAN B reads all zeros (local wiring/module, being fixed) |
| 0 | Pi can0/can1 ERROR-ACTIVE @ 500k (`pi_cockpit/can_up.sh`) | can0 passes (ERROR-ACTIVE, 500 kbit/s, 8 MHz clock, sample point 0.875). can1 pending: only one module wired so far |
| 1 | Bus wiring, 60Ω termination check, common ground | **Done.** Bus A 58 Ω (P1 on N1 and N3); bus B wired 2026-10-20, same layout |
| 1 | candump sees both Nucleos on both buses; jumper-pull test | Bus A: `candump can0` shows `0x720` (N2) and `0x730` (N3) every ~100.2 ms, counters continuous, no error frames; Nucleos hear each other 10/s with 0 TX errors. With the Pi sending `0x710`, all three nodes hear each other on bus A. **N2 bus A jumper pull:** N3 unaffected (ERROR-ACTIVE, 0 errors, still hears N1); N2 isolated (ERROR-PASSIVE via REC=135, tx ok 0, buffers full); re-insert → N2 back to ERROR-ACTIVE, TEC=REC=0, no reset. **Both buses (2026-10-20): done.** All three nodes hear each other on A and B (N2↔N3 10/s, N1 ~9/s from the shell loop), 0 errors. Pulling N2's bus A jumpers: N2's CAN B unaffected; N3 still hears N1 on A and N2 on B; N2's CAN A ERROR-PASSIVE (REC=135). Re-insert: N2 CAN A back to ERROR-ACTIVE, TEC=REC=0, no reset. **Pending: scope captures of CANH/CANL per bus** |
| 2 | Task communication graph figure | Not started |
| 3 | Node RTA (N2, N3), message RTA per bus, end-to-end chains, Pi latency | **Done** (2026-10-21) in `Doc/lab3/lab3_report.md` §3 with measured WCETs (see "WCET measurements"), J_PI = 126.5 µs measured, and measured chain maxima. Brake/throttle, relayed brake, blinker sync and N2 local fail fail by analysis but pass as measured; see the report for the reasons and remedies |
| 4 | Dual-bus + relay design and re-analysis | Not started |
| 5 | Zephyr implementation (dispatch/dedup, send helper, HB_MON, RELAY, error SM, watchdog) | **Running on hardware** (2026-10-21): functional pass done, see "Full-system results". Pending: isolation (d), whole-bus loss (e), scope timing |
| 5 | Pi cockpit (CTRL/BLINK/HB_N1 TX, HB/FORCE RX, bad-input injection) | **Running on the Pi**, builds without warnings. Force is not sent back to the proxy (dummy counter would jerk the wheel) |
| 5 | Test-point breakout boards per node | Not started |
| — | Report, fault-injection matrix, Lab 1 revision-history entry | Not started |

## Revision history

- 2026-10-08: Lab 3 folder created from Lab 2 code; progress doc rewritten for Lab 3.
- 2026-10-21: WCET measured on both zones; main moved to lowest priority; report §3 tables rebuilt from measured values.
