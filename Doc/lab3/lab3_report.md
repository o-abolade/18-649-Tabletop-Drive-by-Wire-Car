<!-- Print layout for Markdown Preview Enhanced / Prince PDF export. -->
<link rel="stylesheet" href="pdf-export.css" media="print">

# 18-449/649 Lab 3: Dual-Redundant CAN and Real-Time Analysis

**Misho Alexandrov (mvalexan), Ore Abolade (oabolade), T'sairus Beasley (tbeasley)**

**System:** N1 cockpit (Raspberry Pi 4), N2 steering and N3 drivetrain
(Nucleo-F401RE, Zephyr), on two 500 kbit/s CAN buses. Every number is
labeled **measured** (with the method) or **assumed** (with the reason). Scope
captures are in Appendix A. The full log is in
`lab3_CAN/Progress_doc_lab3.md`.

## 1. Wiring schematic

![Figure 1: Dual-CAN wiring. Both buses with termination and jumpers, every module's SPI/CS/INT pins, and the three test-point boards](assets/lab3_wiring_schematic.drawio.png)

**Bus layout**
- **Order:** both buses are linear, N1 – N2 – N3.
- **Termination:** 120 Ω at each end, from the P1 pad on N1's and N3's
  modules. Bus A measured 58 Ω with power off.
- **Jumpers:** each CANH and CANL tap has its own jumper, which is the R4.8
  fault-injection point.
- **Trade-off:** P1 sits behind the jumper, so pulling an end node's jumper
  also removes that end's terminator.

**Pins**
- **STM32:** SPI1 is shared (PA5/PA6/PA7). CS is PB6 (A) and PC7 (B); INT is
  PA10 (A) and PB5 (B).
- **Pi:** SPI0 CE0 (A) and CE1 (B). The cockpit picks each bus by chip
  select, because `can0` and `can1` swap between boots.
- **Module pinout:** D9 is MISO and D10 is MOSI; the handout figure has them
  swapped.

**Part 0 and 1, measured**
- **Crystal:** 16 MHz.
- **SPI clock:** 5.25 MHz. The next step, 10.5 MHz, exceeds the MCP2515's
  10 MHz limit.
- **One frame read:** 21 µs on the wire, but one 3-byte register read
  through Zephyr's polled SPI driver takes 30 µs.
- **Zephyr's INT path:** the ISR only gives a semaphore. The SPI reads and
  our dispatch run in the driver's thread at `K_PRIO_COOP(2)`.
- **Part 1 checkpoint:** `candump` saw both Nucleos on both interfaces, and
  a pulled bus A jumper left bus B running.
- **Bus levels:** idle about 2.2 V on both lines; dominant differential
  2.07 V on A and 1.97 V on B (photos 8–9).

## 2. Task communication graph

![Figure 2: Task communication graph, one 20 ms cycle. CAN edges labeled ID, period and deadline; brake/throttle (blue), steering (green), blinker (orange) and error (red) chains highlighted](assets/lab3_task_graph.drawio.png)

- **Edges:** solid edges are CAN frames; dashed edges are hand-offs inside
  one node (semaphores, or state under a spinlock or mutex).
- **Priorities:** the implemented ones are in 3.1. They include the per-bus
  CAN_TX threads and the MCP2515 driver threads, which the template folds
  into CAN_RX.
- **Part 4:** Figure 3 adds N2's RELAY task and the second bus hop.

![Figure 3: Extended task graph for scenario (c): RELAY on N2; relayed brake path 0x010 on A → RELAY → 0x010 on B → DRIVE](assets/lab3_task_graph_with_relay.drawio.png)

## 3. Real-time analysis

The tables below already include both buses and N2's relayed copies, which is
the worst case; on one bus each zone receives fewer frames. A small script
iterates every equation to convergence.

### 3.0 Pi stage (measured)

**Method:** scope Delay statistics from UDP_RX to CMD_TX, 60 s per run, 3000
samples.

| Cockpit scheduling | Load | Max | Mean |
|---|---|---:|---:|
| `--rt` (SCHED_FIFO 80, pinned to CPU 3) | normal (photo 6) | **126.5 µs** | 34.9 µs |
| `--rt` | `stress --cpu 4 --io 2` (photo 7) | 82 µs | 32.1 µs |
| default | normal / stress | 442.5 / 444.5 µs | 53.0 / 104.0 µs |

- **Value used:** **J_PI = 126.5 µs**, the larger of the two `--rt` maxima.
  SCHED_FIFO cut the worst case about 3.5×, so the cockpit always runs with
  `--rt`.
- **No 99th percentile:** the scope reports max and mean only.
- **Not covered:** the laptop → Pi network delay, which comes before UDP_RX.
  The demo uses Ethernet; over Wi-Fi we saw gaps of up to 139 ms.

### 3.1 Node response-time analysis

**How C was measured.**
- **Method:** each job reads its own thread's execution cycles
  (`k_thread_runtime_stats_get`, 84 MHz; `common/zephyr/wcet.c`) at start
  and end. That excludes preemption and includes any ISR that hit the job,
  which is conservative.
- **Value used:** the maximum over every run (driving, blinkers, test
  button, bad input, jumper pulls), printed by each zone. Job counts are in
  the progress doc.
- **Per-frame receive cost:** C_ev = DRV_FRAME + RX_DISPATCH.
  - DRV_FRAME is the MCP2515 thread's run time between two of our dispatches
    on the same bus: its SPI reads plus any TX-done handling.
  - It is taken from a clean 5-minute run with no jumper pulls. While a node
    is unplugged, seconds of error handling fall into one "gap" (254 ms
    seen).
- **Fixed extras included:** RX_DISPATCH and RELAY include the 10 µs
  test-point pulse.
- **Weak sample:** TEST_BTN has only 3–50 presses.

**Assumptions.**
1. **Receive events (catalog maxima).**
   - Each received frame costs C_ev; TX-done for the zone's own frames is
     inside DRV_FRAME.
   - Per 20 ms there are ≤ 24 frames on the two buses: N1's 4 + N2's 3 +
     N3's 2 per bus, plus ≤ 6 relayed copies.
   - So **N2 receives ≤ 12 and N3 ≤ 20**, spaced ≥ 222 µs apart per bus
     (the shortest DLC-8 frame).
   - Event count in a window w: `E(w) = min(2⌈w/222⌉, k⌈w/20 ms⌉)`.
2. **Blocking B (measured).**
   - **STATE_SPI:** the status print's controller reads run with the
     scheduler locked.
   - **`motor_lock` on N3** (priority inheritance): bounded by HB_MON's
     whole C, 232 µs.
3. **ISRs:** they only give semaphores, and their time is already inside
   the measured C values.
4. **Frame-triggered tasks:** R is measured from the end of the triggering
   frame, so it includes that frame's CAN_RX.

`R_i = C_i + B_i + E(R_i)·C_ev + N_tx(R_i)·C_CAN_TX [+ N_relay·C_RELAY on N2] + Σ_hp ⌈R_i/T_j⌉·C_j`

**N2 (steering)** (C, B in µs; R in ms):

| Task | Prio | C | T | B | D | R | Meets? |
|---|---|---:|---|---:|---|---:|---|
| CAN_RX (per frame: 680.5 + 50.9) | coop 2 | 731.4 | 222 µs/bus | 118.5 | 0.444 (2 RX buffers) | 8.90 | **No:** a burst can overflow the RX buffers; the other bus's copy covers |
| CAN_TX ×2 | 0 | 155.8 | per frame | 118.5 | 20 | 9.21 | Yes |
| RELAY | 1 | 86.8 | ≤ 6 / 20 ms | 118.5 | 20 | 10.85 | Yes |
| STEER | 2 | 37.8 | 20 ms | 118.5 | 48.3 | 11.32 | Yes |
| BLINK_F | 3 | 29.2 | 20 ms | 118.5 | 1 (sync) | 11.35 | **No** |
| TEST_BTN | 4 | 65.7 | 50 ms | 118.5 | 10 | 11.42 | **No** (measured 20 µs) |
| HB_MON | 6 | 120.1 | 10 ms | 118.5 | 10 | 11.54 | **No:** one poll late; error chain still meets |
| HB_TX | 7 | 64.6 | 20 ms | 118.5 | 20 | 11.72 | Yes |
| FORCE_TX | 8 | 64.7 | 50 ms | 118.5 | 50 | 11.79 | Yes |

**N3 (drivetrain):**

| Task | Prio | C | T | B | D | R | Meets? |
|---|---|---:|---|---:|---|---:|---|
| CAN_RX (per frame: 452.9 + 50.1) | coop 2 | 503.0 | 222 µs/bus | 120.5 | 0.444 | 10.18 | **No** (as N2) |
| CAN_TX ×2 | 0 | 162.5 | per frame | 120.5 | 20 | 10.51 | Yes |
| DRIVE | 2 | 95.8 | 20 ms | 352.5 | 0.25 (2 ms − R_CTRL) | 11.16 | **No** (chain, 3.3) |
| BLINK_R | 3 | 39.8 | 20 ms | 352.5 | 1 (sync) | 11.20 | **No** |
| TEST_BTN | 4 | 127.9 | 50 ms | 352.5 | 10 | 11.33 | **No** |
| PID | 5 | 122.9 | 10 ms | 352.5 | 10 | 11.45 | **No:** up to 1.5 ms late; uses the real elapsed dt |
| HB_MON | 6 | 232.0 | 10 ms | 120.5 | 10 | 11.57 | **No:** one poll late |
| HB_TX | 7 | 65.2 | 20 ms | 120.5 | 20 | 11.87 | Yes |

**Why tasks miss.** The driver's per-frame cost (503–731 µs) exceeds the
222 µs frame spacing, and two buses feed each zone. In a worst-case burst the
driver threads alone need more than the whole CPU, so every task waits for
the burst to be served: about 9–10 ms. Long-run CPU load is still about 60 %.

**Fixed because of these measurements.** The 1 s status print ran for up to
1.2 ms at Zephyr's default main priority 0, above STEER/DRIVE.
- `main` now drops to the lowest priority after init.
- Its SPI reads run with the scheduler locked. The SPI lock is a `k_sem`
  with no priority inheritance, so a preempted holder would otherwise
  invert priorities.
- N3 prints only after releasing `motor_lock`.

### 3.2 CAN message response times

**Equations:**
`w = B + Σ_hp ⌈(w + J_j + 2 µs)/T_j⌉·C`, then `R = J + w + C`.

**Inputs:**
- **C = 270 µs:** DLC 8 with worst-case stuffing (assumed, computed).
- **hp:** lower IDs, plus other streams with the **same** ID (relayed
  copies, other zones' FAULT), counted conservatively.
- **B:** 270 µs for a lower frame on the wire, plus local single-buffer
  blocking:
  - STM32: one of the sender's own lower frames already in the MCP2515's
    single TX buffer.
  - Pi: up to all of N1's other frames ahead in the FIFO qdisc (assumed).
- **J:**
  - N1 messages: J_PI (measured; assumed equal for HB_N1, BLINK and FAULT,
    which come from the same thread).
  - Zone messages: the R of the sending task.
  - Relayed copies: R_CTRL + R_RELAY = 12.6 ms.

The table is for the busier bus, which carries all 6 relayed copies; the
other bus is a subset of it.

| ID | Message | J | B | w | R | D | Meets? |
|---|---|---:|---:|---:|---:|---:|---|
| 0x010 | CTRL (N1) | 0.13 | 1.08 | 1.35 | **1.75** | 2 | Yes; chain in 3.3 |
| 0x010 | CTRL' (relay) | 12.6 | 0.54 | 0.81 | 13.7 | — | chain in 3.3 |
| 0x020 | FAULT N1 / N2 / N3 | 0.13 / 11.54 / 11.57 | 0.81 / 0.54 / 0.54 | 2.43 / 2.16 / 2.16 | 2.83 / 14.0 / 14.0 | 100 | Yes |
| 0x020 | FAULT N1' / N3' (relay) | 12.6 | 0.54 | 2.16 | 15.0 | 100 | Yes |
| 0x100 | HB_N1 / HB_N1' | 0.13 / 12.6 | 0.54 | 2.70 | 3.10 / 15.6 | 20 | Yes |
| 0x101 | HB_N2 | 11.72 | 0.54 | 2.97 | 15.0 | 20 | Yes |
| 0x102 | HB_N3 / HB_N3' | 11.87 / 12.6 | 0.27 / 0.54 | 3.24 / 3.51 | 15.4 / 16.4 | 20 | Yes (3.6 ms slack) |
| 0x200 | BLINK / BLINK' | 0.13 / 12.6 | 0.27 / 0.54 | 3.78 / 4.05 | 4.18 / 16.9 | 100 | Yes |
| 0x300 | FORCE | 11.79 | 0 | 3.78 | 15.8 | 50 | Yes |

All times are in ms.

**Utilization (R4.6):**
- Busier bus: 14 × 270/20 000 + 270/50 000 = **19.4 %**.
- Other bus: **11.3 %**.
- Both are under the 50 % limit.

**Same-ID frames (assumption):** we assume two same-ID frames never start
arbitration together. A relayed copy trails its original by at least one
relay delay (≥ 474 µs, photo 4), and FAULTs are rare. TEC and REC stayed 0 in
every run.

### 3.3 End-to-end chains

**Bound:** the sum of the stage bounds above. **Measured:** the maximum of
repeated scope captures. Photos 1–5 were taken without `--rt`, so they are
conservative.

| Chain | Bound (stages, ms) | Bound | Measured max | Limit | Verdict |
|---|---|---:|---:|---:|---|
| Brake (R2.2) | CTRL 1.75 + DRIVE 11.16 | 12.9 ms | **940 µs** (photo 1, 15 runs) | 2 ms | **No by analysis; yes as measured** |
| Throttle (R2.1) | same | 12.9 ms | **626 µs** (photo 2, 10 runs) | 2 ms | **No by analysis; yes as measured** |
| Brake via relay (c) | CTRL 1.75 + RELAY 10.85 + CTRL' 1.08 + DRIVE 11.16 | 24.8 ms | **1.1 ms** (photo 3, 10 runs) | 2 ms | **No by analysis; yes as measured.** Degraded latency accepted in (c) (4.3) |
| Steering (R2.3) | CTRL 1.75 + STEER 11.32 | 13.1 ms | — | 50 ms | **Yes** |
| Blinker start (R2.4) | BLINK 4.18 + BLINK_F 11.35 / BLINK_R 11.20 | 15.5 ms | — | 100 ms | **Yes** |
| Blinker sync (R2.4) | max local R 11.35 + skew between bus copies 3.91 | 15.3 ms | **768 µs** (photo 5, 10 edges) | 1 ms | **No by analysis; yes as measured** (232 µs margin) |
| Local fail (R2.5) | TEST_BTN R | 11.4 ms | **20 µs** N2 (photo 12) | 10 ms | **No by analysis; yes as measured** |
| Error, test button (R2.5) | BTN 11.42 + HB_MON 11.54 + FAULT 2.43 + HB_MON 11.57 | 37.0 ms | **1.1 ms** N2 → N3 (photo 13) | 100 ms | **Yes** |
| Error, zone silent (R4.5) | HB R 15.4 + 60 timeout + 10 poll + HB_MON 11.54 | 96.9 ms | 68.4 ms isolation (photo 14) | 100 ms | **Yes** |

**Why the analysis and the measurements disagree.**
- **Cause:** every failing bound comes from one term, the driver burst from
  3.1.
- **Why the captures missed it:** the burst needs CTRL to line up with all
  the heartbeats, BLINK, FAULTs and relayed copies. These are 20 ms streams
  on three independent clocks. The captures saw normal phasing, with one or
  two competing frames.
- **Why it still counts:** clock drift means that alignment eventually
  happens, so the measurements show normal behaviour but don't guarantee
  the deadline. Even without FAULT traffic, DRIVE's bound is 8.1 ms.

**Remedies (not implemented; we kept Zephyr's MCP2515 driver):**
1. **Cut the driver's per-frame cost.** Use a lean receive path (READ RX
   BUFFER on RXnIF), SPI DMA, or hardware acceptance filters. This is the
   dominant term.
2. **Relay only after a bus is seen degraded.** This removes 6 received
   frames per 20 ms, but adds the 60 ms detection time to the first relayed
   frame.
3. **Priority qdisc on the Pi,** with CTRL in the top band. R_CTRL would drop
   from 1.75 to 0.94 ms.

## 4. Dual-redundant CAN and the relay case

### 4.1 Hot/parallel transmit

**What the code does:**
- **Send:** each message is queued on A and then B, with one sequence
  number (`can_link_send`).
- **Receive:** one dispatch path keeps the first copy by `(ID, seq)`. FAULT
  gets one dedup slot per sender.
- **Cost:** every message is 2 CAN_TX jobs and 2 receive events, and the
  second event still pays C_ev. Section 3 already charges both copies, so its
  tables are the 4.1 re-analysis.

**The chains get no worse,** because each receiver acts on the first copy and
the duplicate only adds one interference term. That assumes:
- the two copies leave together (the Pi writes A then B inside the measured
  126.5 µs stage);
- dropping a duplicate is cheap: a table lookup inside RX_DISPATCH, ≤ 51 µs.

### 4.2 Relay design

| Question | Decision | Why |
|---|---|---|
| Who relays? | Only N2 | In the handout's (c) it is the only node on both buses. See the pull list in 4.3 for the cost. |
| When? | Always, with no detection | Waiting for detection would cut CTRL (brakes included) for 60–70 ms. Costs bus time: worst bus 11.3 → 19.4 %. |
| What? | The first copy of every N1/N3 frame (CTRL, BLINK, HB_N1, HB_N3, FAULT), onto the other bus | N2's own frames are already on both buses. |
| Loops? | Relayed flag 0x10 in byte 0; N2 never relays a flagged frame | The sequence number is kept, so receivers dedup with their existing state. The CTRL CRC masks the flags. |
| Priority? | RELAY = 1: above STEER and all app tasks, below CAN_TX and the driver threads | It's on the brake path. The receive hook only queues (16 deep). TX queues are sorted by ID, so a relayed CTRL overtakes waiting HB/FORCE. |
| Cost to N2 | ≤ 6 × 86.8 µs + 6 CAN_TX per 20 ms | Already in N2's table. STEER still meets. |

### 4.3 Re-analysis for scenario (c): N1 off B, N3 off A

**Traffic:** each bus carries 9 unique frames per 20 ms, giving **11.3 %
utilization**.
- **Bus A:** N1's 4 + N2's 3 + N3's 2 relayed.
- **Bus B:** N2's 3 + N3's 2 + N1's 4 relayed.
- **Receivers:** N2 receives 6 frames and N3 receives 7, all on B.

**Assumption:** N3's unacknowledged bus A sends are counted as CAN_TX jobs,
but the unplugged module's error interrupts aren't modelled. The relayed
chain measurement (photo 3) covers them.

| N2 task | R (ms) | Meets? | N3 task | R (ms) | Meets? |
|---|---:|---|---|---:|---|
| RELAY / STEER | 6.46 / 6.94 | Yes / Yes | DRIVE | 4.62 | chain (No) |
| BLINK_F | 6.96 | No (sync) | BLINK_R | 4.66 | No (sync) |
| TEST_BTN | 7.03 | **Yes** | TEST_BTN / PID / HB_MON | 4.79 / 4.91 / 4.91 | **Yes** |
| HB_MON / HB_TX / FORCE_TX | 7.15 / 7.21 / 7.28 | Yes | HB_TX | 4.98 | Yes |

| Message R (ms) | Bus A | Bus B | D | Meets? |
|---|---|---|---:|---|
| CTRL / CTRL' | 1.48 (J 0.13) | 8.75 (J 7.94 = 1.48 + RELAY 6.46) | chain | chain |
| FAULT | N1 2.02, N2 8.77, N3' 15.2 | N1' 9.56, N2 8.77, N3 6.53 | 100 | Yes |
| HB_N1 / HB_N2 / HB_N3 | 2.02 / 9.37 / 16.0 (relayed) | 9.83 (relayed) / 9.37 / 7.14 | 20 | Yes |
| BLINK / FORCE | 2.56 / 9.71 | 10.6 (relayed) / 9.71 | 100 / 50 | Yes |

| Chain in (c) | Stages (ms) | Bound | Measured | Limit | Verdict |
|---|---|---:|---:|---:|---|
| Brake / throttle (relayed) | CTRL 1.48 + RELAY 6.46 + CTRL' 0.81 + DRIVE 4.62 | 13.4 ms | **1.1 ms** (photo 3) | 2 ms | **No by analysis; yes as measured** |
| Steering | CTRL 1.48 + STEER 6.94 | 8.4 ms | — | 50 ms | Yes |
| Blinker start, front / rear | via A / via relay | 9.5 / 16.4 ms | — | 100 ms | Yes |
| Blinker sync | RELAY 6.46 + BLINK' 2.70 + BLINK_R 4.66 | 13.8 ms | — | 1 ms | **No by analysis** |
| Error: N2 button / N1 dies | 7.03 + 7.15 + 1.62 + 4.91 / HB_N1' 9.83 + 60 + 10 + 4.91 | 20.7 / 84.7 ms | — | 100 ms | Yes |

**Verdict per chain:**
- **Brake / throttle:** fails by analysis, measures 1.1 ms (retaken after the
  bus B fix, same result).
  - What changed: an always-on relay, RELAY above every app task, and
    ID-sorted TX queues.
  - What's left is the driver cost from 3.1. We take the handout's option to
    **accept documented degraded latency in (c)**, which is a double fault.
- **Steering and blinker start:** meet with wide margin.
- **Blinker sync:** N3 can only act on N2's relayed copy, so even the
  typical offset is the relay delay (about 0.5–0.8 ms).
  - Remedy: N2 delays its own edge by the relay latency while it relays.
  - Accepted as degraded in (c).
- **Error propagation:** meets.
  - Without the relay, N3 would declare N1 failed after 60–70 ms. The relay
    keeps N3 NORMAL; we measured `HB N1:-B` in (c).
  - Relayed HB_N1 copies reach N3 10.4–29.6 ms apart. That's outside
    18–22 ms (the ±10 % rule applies at the sender, N1, which still sends
    every 20 ms), but well inside the 60 ms missed-heartbeat limit.

**Which jumper pulls need a relay:**

| Fault | Relay needed? | Our system |
|---|---|---|
| One node off one bus (a/b), two nodes off the same bus, whole bus (e) | No | Fully operational (tested) |
| N1 and N3 off opposite buses (handout (c)) | Yes, via N2 | **Fully operational** (tested) |
| N2 and N3 off opposite buses (Lab 1's R4.8(c) example) | Yes, via N1 | **Not supported:** system ERROR |
| N1 and N2 off opposite buses | Yes, via N3 | **Not supported:** system ERROR |
| One node off both buses (d) | No relay helps | Node fail-safe; others ERROR (tested) |

**Remedy (not implemented):** run the same relay rule on **every** node that
is on both buses. It stays loop-free because flagged frames are never
forwarded, at the cost of more bus load.

## 5. Fault-injection test matrix (R4.8 a–f)

**How faults were injected:** by pulling a node's CANH/CANL jumpers. Pulling
N1's or N3's jumper also removes that end's P1 terminator (Section 1).

**Detection design:** a bus is degraded after 60 ms with no traffic heard;
HB_MON checks every 10 ms. A zone is failed after 60 ms without its
heartbeat on both buses.

**Sources:** functional results come from the zone consoles, the Pi status
line and the LEDs; timing results are scope maxima.

| | Fault | Expected: zones | Expected: LEDs | Measured: function | Measured: time |
|---|---|---|---|---|---|
| a | One node off A (demo N2) | All NORMAL, fully operational | That node DEG_A | ✅ N2 B unaffected, N1/N3 unaffected | DEG_A on **68.6 ms** max over 5 (photo 10); design 60–70 ms |
| b | One node off B | All NORMAL | That node DEG_B | ✅ (seen for N1 in (c); same code path as a) | — |
| c | N1 off B + N3 off A | All NORMAL via N2's relay | N1 DEG_B, N3 DEG_A | ✅ N3 drives on relayed CTRL; 0 relay drops | Relayed brake **1.1 ms** (photo 3); relay stage 607 µs (photo 4) |
| d | N2 off both buses | N2 fail-safe (steering held); N1, N3 ERROR (hazards, N3 braked) | N2 DEG_A + DEG_B; others none | ✅ | N3 ERROR **68.4 ms** (photo 14); N2 ERROR **53.6 ms** (photo 15); limit 100 ms |
| e | Every node off A | All NORMAL on B | All DEG_A | ✅ CTRL gaps 36–39 ms (needed the Pi TX watchdog) | — |
| f | Restore jumpers | Back to NORMAL, no reset; MCP2515 auto bus-off recovery, Pi `restart-ms 100` | DEG off | ✅ every case | DEG off **14.8 ms** (photo 11, limit 100); recovery N2 **15.6 ms** / N3 **16.5 ms** (photos 16–17, limit 500) |

## 6. Team-defined values and Lab 1 revision

`Doc/Requirement_Document.md` has been updated for Lab 3:
- **Section 6:** the template catalog, with our payloads, handling rules and
  utilization.
- **Section 7:** a "Lab 3 values" table with every threshold and mapping
  and its source file.
- **Revision history:** a 2026-10-21 entry, including the relay limitation.

## Appendix A: Scope captures

Every value is the **maximum over the repeats** listed. Test-point rows refer
to Figure 1. Photos 1–5 were taken with the cockpit **without** `--rt`, so
their Pi stage is the slower one (up to about 443 µs) and the chain values are
conservative.

### Brake chain (R2.2), Pi UDP_RX → N3 DIR_A. One capture 640 µs; max 940 µs over 15 counts
![Photo 1: Brake chain (R2.2), Pi UDP_RX → N3 DIR_A. One capture 640 µs; max 940 µs over 15](assets/lab3_scope/photo01.jpeg)

### Throttle chain (R2.1), Pi UDP_RX → N3 PWM_SET (first pulse). One capture 614 µs; max 626 µs over 10 counts
![Photo 2: Throttle chain (R2.1), Pi UDP_RX → N3 PWM_SET (first pulse). One capture 614 µs; max 626 µs over 10](assets/lab3_scope/photo02.jpeg)

### Relayed brake chain, scenario (c), Pi UDP_RX → N3 DIR_A via N2 RELAY. Max 1.1 ms over 10 counts
![Photo 3: Relayed brake chain, scenario (c), Pi UDP_RX → N3 DIR_A via N2 RELAY. Max 1.1 ms over 10](assets/lab3_scope/photo03.jpeg)

### Relay stage, scenario (c), Pi UDP_RX → N2 RELAY_TX. One capture 474 µs; max 607 µs over 10 counts
![Photo 4: Relay stage, scenario (c), Pi UDP_RX → N2 RELAY_TX. One capture 474 µs; max 607 µs over 10](assets/lab3_scope/photo04.jpeg)

### Blinker sync (R2.4), N2 FL vs N3 RL rising edges. One capture 186 µs; max 768 µs over 10 counts
![Photo 5: Blinker sync (R2.4), N2 FL vs N3 RL rising edges. One capture 186 µs; max 768 µs over 10 edges](assets/lab3_scope/photo05.jpeg)

### Pi stage UDP_RX → CMD_TX, cockpit --rt, normal load. Max 126.5 µs, mean 34.9 µs, 3000 samples
![Photo 6: Pi stage UDP_RX → CMD_TX, cockpit --rt, normal load. Max 126.5 µs, mean 34.9 µs, 3000 samples](assets/lab3_scope/photo06.jpeg)

### Pi stage, cockpit --rt, under stress --cpu 4 --io 2. Max 82 µs, mean 32.1 µs, 3000 samples
![Photo 7: Pi stage, cockpit --rt, under stress --cpu 4 --io 2. Max 82 µs, mean 32.1 µs, 3000 samples](assets/lab3_scope/photo07.jpeg)

### CAN A CANH/CANL. Idle 2.18/2.21 V; dominant 2.68/0.61 V, differential 2.07 V
![Photo 8: CAN A CANH/CANL. Idle 2.18/2.21 V; dominant 2.68/0.61 V, differential 2.07 V](assets/lab3_scope/photo08.jpeg)

### CAN B CANH/CANL. Idle 2.20/2.19 V; dominant 2.70/0.73 V, differential 1.97 V
![Photo 9: CAN B CANH/CANL. Idle 2.20/2.19 V; dominant 2.70/0.73 V, differential 1.97 V](assets/lab3_scope/photo09.jpeg)

### (a) N2 off CAN A, last CAN_RX_A → DEG_A on. 62.8 ms; max 68.6 ms over 5 counts
![Photo 10: (a) N2 off CAN A, last CAN_RX_A → DEG_A on. 62.8 ms; max 68.6 ms over 5](assets/lab3_scope/photo10.jpeg)

### (f) N2 bus A restored, first CAN_RX_A → DEG_A off. 9.6 ms; max 14.8 ms over 5 counts
![Photo 11: (f) N2 bus A restored, first CAN_RX_A → DEG_A off. 9.6 ms; max 14.8 ms over 5](assets/lab3_scope/photo11.jpeg)

### R2.5 local fail, N2 B1 press → N2 ERR. Max 20 µs over 5 counts
![Photo 12: R2.5 local fail, N2 B1 press → N2 ERR. Max 20 µs over 5](assets/lab3_scope/photo12.jpeg)

### R2.5 system error, N2 B1 press → N3 ERR. 1.0 ms; max 1.1 ms over 5 counts
![Photo 13: R2.5 system error, N2 B1 press → N3 ERR. 1.0 ms; max 1.1 ms over 5](assets/lab3_scope/photo13.jpeg)

### (d) N2 off both buses, last N2 CAN_RX_B → N3 ERR. 59.4 ms; max 68.4 ms over 3 counts
![Photo 14: (d) N2 off both buses, last N2 CAN_RX_B → N3 ERR. 59.4 ms; max 68.4 ms over 3](assets/lab3_scope/photo14.jpeg)

### (d) isolated N2 enters fail-safe, last CAN_RX_B → N2 ERR. 52.4 ms; max 53.6 ms over 3 counts
![Photo 15: (d) isolated N2 enters fail-safe, last CAN_RX_B → N2 ERR. 52.4 ms; max 53.6 ms over 3](assets/lab3_scope/photo15.jpeg)

### (f) N2 jumpers restored, first CAN_RX_A → N2 ERR off. 3.6 ms; max 15.6 ms over 3 counts
![Photo 16: (f) N2 jumpers restored, first CAN_RX_A → N2 ERR off. 3.6 ms; max 15.6 ms over 3](assets/lab3_scope/photo16.jpeg)

### (f) N2 restored, first N2 CAN_RX_A → N3 ERR off. Max 16.5 ms over 3 counts
![Photo 17: (f) N2 restored, first N2 CAN_RX_A → N3 ERR off. Max 16.5 ms over 3](assets/lab3_scope/photo17.jpeg)
