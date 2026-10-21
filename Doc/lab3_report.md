
## Section 3 ##

Every number below is labeled **measured** (with the method) or **assumed**
(with the reason). Times are in µs unless marked ms. The calculations were
run with a small script that iterates the equations below until they
converge, so the R values are exact for the stated inputs.

### 3.1 Node response-time analysis (N2, N3)

#### How C was measured

- **Method (measured): Zephyr thread runtime statistics.** At the start and
  end of each job, the firmware reads the current thread's accumulated
  execution cycles (`k_thread_runtime_stats_get`, 84 MHz cycle counter;
  `common/zephyr/wcet.c`). The difference is the time that thread itself
  ran, so preemption by other threads is excluded. Any ISR that ran during
  the job is charged to the thread, so ISR time is included (conservative).
  Each zone keeps the maximum per task since boot and prints it every 5 s.
- **Load during the runs:** full system on hardware (Pi cockpit with `--rt`,
  wheel proxy at 50 packets/s, both buses), while exercising throttle, brake,
  steering, both blinkers and hazard, the test button (single and double
  press), bad-input injection and bus-jumper pulls. Later runs were taken on
  the final firmware, including one after bus B's CANL wiring was fixed
  (it had been shorted to ground) with no jumper pulls. Each task's C is the
  **largest value seen in any run**; jumper pulls don't make a job's own
  execution time invalid. Job counts are from the run that produced the
  maximum.
- **MCP2515 driver work per frame (`DRV_FRAME`).** The Zephyr MCP2515 driver
  reads the chip from its own cooperative thread (`K_PRIO_COOP(2)`), and our
  dispatch (`RX_DISPATCH`) runs inside that thread. `DRV_FRAME` is the
  driver thread's run time from the end of one `RX_DISPATCH` to the start of
  the next on the same bus. That covers the driver's SPI reads for the next
  frame, plus any TX-done or error handling in between.
  - **Per-frame cost:** `C_ev = DRV_FRAME + RX_DISPATCH`, charged once per
    **received** frame. TX-done handling for the zone's own frames is
    already inside DRV_FRAME, so own frames aren't charged again.
  - **DRV_FRAME is only valid with no jumper pulls.** While a node is
    unplugged it receives nothing for seconds while the driver handles error
    interrupts, and all of that lands in one gap (seen: 254 ms). The value
    used is the maximum of the clean run: bus B fixed, about 5 minutes of
    driving, blinkers and test-button presses.
  - **Why it is so large (measured):** one 3-byte SPI register read takes
    **30 µs**, against 4.6 µs of wire time at 5.25 MHz (timed in the init
    probe, `can_link.c`). A frame needs 3–4 such polled transactions, so
    per-transaction driver overhead dominates.
- **Some numbers include fixed extras (conservative):**
  - `RX_DISPATCH` and `RELAY` include the ≥10 µs test-point pulse.
  - `HB_MON` and `TEST_BTN` include a `printk` on state changes. With
    deferred logging, that cost is only formatting into the log buffer.
- **`TEST_BTN` has few jobs** (3–50 button presses). That max is a weak sample.

| Task | Zone | C max (measured) | Jobs | Notes |
|---|---|---:|---:|---|
| C_ev = DRV_FRAME + RX_DISPATCH | N2 | 680.5 + 50.9 = **731.4** | 96 007 / 95 367 | DRV_FRAME from the clean run |
| C_ev = DRV_FRAME + RX_DISPATCH | N3 | 452.9 + 50.1 = **503.0** | 164 313 / 145 575 | DRV_FRAME from the clean run |
| CAN_TX (one frame into the MCP2515) | N2 / N3 | 155.8 / 162.5 | 99 681 / 357 784 | time asleep waiting for the TX buffer not counted |
| RELAY | N2 | 86.8 | 468 206 | |
| STEER / DRIVE | N2 / N3 | 37.8 / 95.8 | 20 805 / 8 056 | |
| BLINK_F / BLINK_R | N2 / N3 | 29.2 / 39.8 | 204 / 186 | |
| TEST_BTN (thread) | N2 / N3 | 65.7 / 127.9 | 50 / 3 | accepted presses |
| PID | N3 | 122.9 | 33 527 | |
| HB_MON | N2 / N3 | 120.1 / 232.0 | 42 023 / 357 785 | |
| HB_TX | N2 / N3 | 64.6 / 65.2 | 3 252 / 178 892 | |
| FORCE_TX | N2 | 64.7 | 8 404 | |
| STATE_SPI (scheduler locked) | N2 / N3 | 118.5 / 120.5 | 7 140 / 7 150 | the only non-preemptible section below CAN_TX |
| MCP2515 thread longest single run | N2 / N3 | 489.5 / 450.0 | — | Zephyr `peak_cycles`; one wake-up may serve several frames |

#### Task set and priorities (from `common/zephyr/zone_config.h`)

Zephyr: a lower number means a higher priority. Cooperative threads outrank
all preemptive ones and are not preempted by them.

| Prio | Task | Release | T (min) |
|---|---|---|---|
| coop 2 | MCP2515 driver thread ×2 (one per bus): SPI read + `RX_DISPATCH` | every frame on its bus (RX, or TX-done of our own frame) | 222 µs per bus |
| 0 | CAN_TX ×2: per-bus queue sorted by CAN ID → the MCP2515's single TX buffer | each frame this zone sends | 222 µs per bus |
| 1 | RELAY (N2) | each relayed frame | ≤ 6 per 20 ms |
| 2 | STEER (N2) / DRIVE (N3) | CTRL | 20 ms |
| 3 | BLINK_F / BLINK_R | BLINK (hazard: 250 ms) | 20 ms |
| 4 | TEST_BTN (thread; the ISR only gives a semaphore) | button press | 50 ms (debounce) |
| 5 | PID (N3) | timer | 10 ms |
| 6 | HB_MON | timer (and early on FAULT / bad CTRL) | 10 ms |
| 7 | HB_TX | timer | 20 ms |
| 8 | FORCE_TX (N2) | timer | 50 ms |
| lowest | `main` status print | timer | 1 s |

#### Assumptions

1. **CAN frame events (assumed, catalog maxima).** Every frame a zone
   receives costs it one driver event, C_ev. The TX-done handling for its
   own frames is inside DRV_FRAME.
   - **Per bus per 20 ms, original frames:**
     - N1: CTRL, HB_N1, BLINK, FAULT;
     - N2: HB_N2, FAULT, FORCE;
     - N3: HB_N3, FAULT.
   - **Relayed copies:** N2 relays each N1/N3 message once, onto the bus it
     didn't arrive on. That is ≤ 6 per 20 ms, and all 6 can land on the same
     bus.
   - **Totals:** ≤ 24 frames per 20 ms over both buses (≤ 15 on the busier
     bus). N2 sends 12 of them itself, so it **receives ≤ 12**. N3 sends 4,
     so it **receives ≤ 20**.
   - **Spacing:** events on one bus are ≥ 222 µs apart. That is the shortest
     DLC-8 frame: 111 bits × 2 µs, no stuffing, interframe space included.
   - **Event count in a window w:** `E(w) = min(2·⌈w/222⌉, k·⌈w/20 ms⌉)`,
     with k = 12 for N2 and 20 for N3.
   - **Own frames sent (CAN_TX jobs):** N2 sends 12 per 20 ms (HB, FAULT,
     FORCE on both buses + 6 relays); N3 sends 4.
2. **Blocking B (measured):**
   - **SPI lock.** It is a `k_sem`, with no priority inheritance. The only
     lower-priority SPI user is the status print. It now reads the
     controllers with the scheduler locked, so it blocks every thread at
     most once, for `STATE_SPI`.
   - **`motor_lock` on N3.** It is a `k_mutex`, with priority inheritance.
     Its users below DRIVE are TEST_BTN, PID and HB_MON (through the
     safe-state hook) and `main` (copying 4 values). DRIVE, BLINK_R and
     TEST_BTN can be blocked once by the longest of those critical sections,
     bounded here by the whole job C of HB_MON (232.0). PID is blocked by at
     most HB_MON.
3. **ISRs (assumed short):** the INT, button and timer ISRs only give a
   semaphore. ISR time is charged to whatever thread is running, so it is
   already inside the measured C values. No separate term is added.
4. **Releases tied to the triggering frame:** for STEER, DRIVE, BLINK and
   RELAY, R is measured from the **end of the triggering frame on the bus**,
   so it includes that frame's own driver event. The chains in 3.3 therefore
   add R directly, with no separate CAN_RX stage.
5. **Local deadlines** are derived from the requirements: the chain budget
   left after the upstream stages, or the task's period when it has no
   chain.

#### Equation

`R_i = C_i + B_i + E(R_i)·C_ev + N_tx(R_i)·C_CAN_TX [+ N_relay(R_i)·C_RELAY on N2] + Σ_{hp j} ⌈R_i/T_j⌉·C_j`

The event terms drive the result:
- A zone's per-frame driver cost (503–731 µs) is larger than the 222 µs
  minimum spacing between frames on one bus, and two buses feed it.
- So during a worst-case burst the driver threads alone need more than 100 %
  of the CPU, and every preemptive task waits until the burst's received
  frames are served (12 × 731 = 8.8 ms on N2, 20 × 503 = 10.1 ms on N3).
- Long-run utilization is still below 1: about 60 % on both N2 and N3.

#### Node 2 (steering)

| Task | Prio | C (measured) | T | B (measured) | D (local) | R | Meets D? |
|---|---|---:|---:|---:|---:|---:|---|
| CAN_RX (one frame) | coop 2 | 731.4 | 222 µs/bus | 118.5 | 444 µs (2 RX buffers) | 8.90 ms | **No.** In a worst-case burst more than 2 frames can wait per controller, so frames can be lost on one bus. The duplicate on the other bus is then the copy that gets through |
| CAN_TX | 0 | 155.8 | per frame | 118.5 | 20 ms | 9.21 ms | Yes: feeds J in 3.2 |
| RELAY | 1 | 86.8 | ≤ 6 / 20 ms | 118.5 | 20 ms | 10.85 ms | Yes, but the relayed brake chain fails (3.3) |
| STEER | 2 | 37.8 | 20 ms | 118.5 | 48.3 ms (50 ms − R_CTRL) | 11.32 ms | Yes, 37 ms slack |
| BLINK_F | 3 | 29.2 | 20 ms | 118.5 | 1 ms (sync) | 11.35 ms | **No:** waits behind the CAN burst |
| TEST_BTN | 4 | 65.7 | 50 ms | 118.5 | 10 ms (R2.5) | 11.42 ms | **No:** waits behind the CAN burst (measured 20 µs, photo 12) |
| HB_MON | 6 | 120.1 | 10 ms | 118.5 | 10 ms (period) | 11.54 ms | **No:** a poll can slip by one period. The error chain still meets (3.3) |
| HB_TX | 7 | 64.6 | 20 ms | 118.5 | 20 ms | 11.72 ms | Yes |
| FORCE_TX | 8 | 64.7 | 50 ms | 118.5 | 50 ms | 11.79 ms | Yes |

#### Node 3 (drivetrain)

| Task | Prio | C (measured) | T | B (measured) | D (local) | R | Meets D? |
|---|---|---:|---:|---:|---:|---:|---|
| CAN_RX (one frame) | coop 2 | 503.0 | 222 µs/bus | 120.5 | 444 µs (2 RX buffers) | 10.18 ms | **No:** same as N2 |
| CAN_TX | 0 | 162.5 | per frame | 120.5 | 20 ms | 10.51 ms | Yes: feeds J in 3.2 |
| DRIVE | 2 | 95.8 | 20 ms | 120.5 + 232.0 | 0.25 ms (2 ms − R_CTRL) | 11.16 ms | **No:** the brake/throttle chain fails by analysis (3.3) |
| BLINK_R | 3 | 39.8 | 20 ms | 120.5 + 232.0 | 1 ms (sync) | 11.20 ms | **No** |
| TEST_BTN | 4 | 127.9 | 50 ms | 120.5 + 232.0 | 10 ms (R2.5) | 11.33 ms | **No:** waits behind the CAN burst |
| PID | 5 | 122.9 | 10 ms | 120.5 + 232.0 | 10 ms | 11.45 ms | **No:** a tick can run up to 1.5 ms late once per burst. PID uses the real elapsed dt, so a late tick doesn't corrupt the control output |
| HB_MON | 6 | 232.0 | 10 ms | 120.5 | 10 ms | 11.57 ms | **No:** a poll can slip by one period. The error chain still meets (3.3) |
| HB_TX | 7 | 65.2 | 20 ms | 120.5 | 20 ms | 11.87 ms | Yes |

#### Changes made because of these measurements

The first WCET run showed that the `main` status print took up to **1.2 ms**
(measured) at Zephyr's default main priority 0. That is above STEER/DRIVE,
once a second. Fixed on both zones:

1. `main` drops to the lowest priority after init.
2. Its two controller-state SPI reads run with the scheduler locked. A
   preempted `main` could otherwise hold the SPI semaphore while
   middle-priority threads ran, which is unbounded priority inversion,
   since a `k_sem` has no inheritance.
3. N3 copies the drive state under `motor_lock` and prints after releasing
   it.
4. The WCET print walks threads with `k_thread_foreach_unlocked`, so
   interrupts are never off during `printk`.

### 3.2 CAN message response times

#### Model

- **Bus:** 500 kbit/s, every catalog frame DLC 8. Worst-case frame time
  **C = 270 µs** (assumed: computed, 135 bits with worst-case stuffing).
- **Worst-case bus:** the table is for the busier bus, the one carrying all
  6 relayed copies (15 frames per 20 ms). The other bus carries a subset of
  these frames, so its R values are smaller.

#### Equations

`w_m = B_m + Σ_{j ∈ hp(m)} ⌈(w_m + J_j + 2 µs)/T_j⌉ · C_j`

`R_m = J_m + w_m + C_m`

The terms:
- **hp(m):** frames with a lower ID. It also includes the other streams with
  the **same** ID (a relayed copy, or another zone's FAULT). Those are
  counted conservatively as interference.
- **B_m = bus blocking + local blocking.**
  - **Bus blocking:** one lower-priority frame already on the wire, 270 µs.
    It is 0 for FORCE, which has no lower-priority frame.
  - **Local blocking (STM32):** the MCP2515 uses **one TX buffer**, so one
    of the sender's own lower-priority frames may already be in it: 270 µs,
    if the sender has a lower-priority frame. Measured on hardware: with a
    queue that didn't sort by ID, N2 dropped about 10 frames/s.
  - **Local blocking (Pi):** `mcp251x` also sends one frame at a time, and
    its default qdisc is FIFO. So up to all of N1's other frames can be
    ahead of the message: 270 µs each (assumed: qdisc behavior, not scoped).
- **J (release jitter):**
  - **N1 messages:** `J_PI` = **126.5 µs, measured**. That is the maximum
    Pi `UDP_RX` → `CMD_TX` delay over 3000 samples with `cockpit --rt`
    (SCHED_FIFO 80, CPU 3; photo 6). The maximum was 82 µs under
    `stress --cpu 4 --io 2` (photo 7). The same value is assumed for
    HB_N1, BLINK and FAULT_N1, which come from the same thread.
  - **Zone messages:** the R of the sending task (3.1). That R includes
    CAN_TX and the TX-done handling that frees the buffer.
  - **Relayed copies:** `J_relay = R_CTRL + R_RELAY` = 1.75 + 10.85 =
    12.6 ms. That is the original on the other bus plus N2's relay stage.

#### Results

| ID | Message (sender) | T | J | B | w | R | D | Meets D? |
|---|---|---:|---:|---:|---:|---:|---:|---|
| 0x010 | CTRL (N1) | 20 ms | 126.5 µs (measured) | 270 + 3×270 | 1.35 ms | **1.75 ms** | 2 ms | Yes on its own. The full chain fails (3.3) |
| 0x010 | CTRL' (N2 relay) | 20 ms | 12.6 ms | 270 + 270 | 0.81 ms | 13.7 ms | — | used in the relayed chain (3.3) |
| 0x020 | FAULT_N1 | 20 ms | 126.5 µs | 270 + 2×270 | 2.43 ms | 2.83 ms | 100 ms | Yes |
| 0x020 | FAULT_N2 | 20 ms | 11.54 ms (N2 HB_MON) | 270 + 270 | 2.16 ms | 14.0 ms | 100 ms | Yes |
| 0x020 | FAULT_N3 | 20 ms | 11.57 ms (N3 HB_MON) | 270 + 270 | 2.16 ms | 14.0 ms | 100 ms | Yes |
| 0x020 | FAULT_N1' / FAULT_N3' (relay) | 20 ms | 12.6 ms | 270 + 270 | 2.16 ms | 15.0 ms | 100 ms | Yes |
| 0x100 | HB_N1 | 20 ms | 126.5 µs | 270 + 270 | 2.70 ms | 3.10 ms | 20 ms | Yes |
| 0x100 | HB_N1' (relay) | 20 ms | 12.6 ms | 270 + 270 | 2.70 ms | 15.6 ms | 20 ms | Yes, 4.4 ms slack |
| 0x101 | HB_N2 | 20 ms | 11.72 ms (N2 HB_TX) | 270 + 270 | 2.97 ms | 15.0 ms | 20 ms | Yes, 5.0 ms slack |
| 0x102 | HB_N3 | 20 ms | 11.87 ms (N3 HB_TX) | 270 | 3.24 ms | 15.4 ms | 20 ms | Yes, 4.6 ms slack |
| 0x102 | HB_N3' (relay) | 20 ms | 12.6 ms | 270 + 270 | 3.51 ms | 16.4 ms | 20 ms | Yes, 3.6 ms slack |
| 0x200 | BLINK (N1) | 20 ms | 126.5 µs | 270 | 3.78 ms | 4.18 ms | 100 ms | Yes |
| 0x200 | BLINK' (relay) | 20 ms | 12.6 ms | 270 + 270 | 4.05 ms | 16.9 ms | 100 ms | Yes |
| 0x300 | FORCE (N2) | 50 ms | 11.79 ms (N2 FORCE_TX) | 0 | 3.78 ms | 15.8 ms | 50 ms | Yes |

#### Bus utilization (R4.6, ≤ 50 % per bus)

- **Busier bus:** 14 frames per 20 ms plus FORCE per 50 ms:
  `U = 14·270/20 000 + 270/50 000 = 19.4 %`.
- **Other bus:** `U = 8·270/20 000 + 270/50 000 = 11.3 %`.
- **Verdict: both meet.**

#### Same-ID frames (assumption)

Several messages share an ID: FAULT from three senders, and every relayed
copy has the same ID as its original. If two of these started arbitration
at the same bit, they would collide in the data field. That would cause an
error frame and a retry.

The analysis assumes they never start together:
- A relayed copy goes out at least one N2 receive-and-relay delay after the
  original (474 µs in photo 4).
- FAULTs are rare events.

Every run in this lab showed TEC = REC = 0 on both buses, for every node.

### 3.3 End-to-end chains

#### How the chains are built

- **Bound:** the sum of the stage bounds above. Node stages are measured
  from the end of the triggering frame on the bus, so CAN_RX isn't added
  twice.
- **Measured:** the maximum over repeated scope captures. Photos 1–5 are
  from the scope capture log in `lab3_CAN/Progress_doc_lab3.md`. They were
  taken **without** `--rt` on the Pi (Pi stage up to about 443 µs instead of
  126.5 µs), so they are conservative.

#### Results

| Chain | Path and calculation (bound) | Bound | Measured (max) | Deadline | Verdict |
|---|---|---:|---:|---:|---|
| Brake (R2.2) | R_CTRL 1.75 + N3 DRIVE 11.16 | 12.90 ms | **940 µs** (photo 1, 15 captures, UDP_RX → DIR_A) | 2 ms | **Doesn't meet by analysis; meets as measured** (see below) |
| Throttle (R2.1) | same as brake | 12.90 ms | **626 µs** (photo 2, 10 captures, UDP_RX → PWM_SET) | 2 ms | **Doesn't meet by analysis; meets as measured** |
| Brake via relay, scenario (c) | R_CTRL 1.75 + N2 RELAY 10.85 + CTRL' on the other bus (w + C) 1.08 + N3 DRIVE 11.16 | 24.8 ms | **1.1 ms** (photo 3, 10 captures); relay stage 607 µs (photo 4) | 2 ms | **Doesn't meet by analysis; meets as measured.** Chosen remedy (handout 4.3 option): accept documented degraded latency in scenario (c), a double fault (N1 off B and N3 off A) |
| Steering | R_CTRL 1.75 + N2 STEER 11.32 | 13.1 ms | not scoped | 50 ms | **Meets**, 37 ms slack |
| Blinker start, front / rear | R_BLINK 4.18 + BLINK_F 11.35 / BLINK_R 11.20 | 15.5 / 15.4 ms | not scoped (functional pass) | 100 ms | **Meets** |
| Blinker synchronization (R2.4) | Edge offset ≤ max local R (11.35) + skew between the first copies each zone accepts (R_BLINK − C = 3.91). N2 and N3 can accept copies from different buses, and each copy arbitrates on its own bus | 15.3 ms | **768 µs** (photo 5, 10 edges, FL vs RL) | 1 ms | **Doesn't meet by analysis; meets as measured, with only 232 µs margin** |
| Local fail (R2.5), N2 / N3 | TEST_BTN R | 11.4 / 11.3 ms | N2: **20 µs** max over 5 presses (photo 12, B1 → ERR); N3 not scoped | 10 ms | **Doesn't meet by analysis (by 1.4 ms); N2 meets as measured** |
| Error propagation, test button (FAULT path) | press → TEST_BTN → HB_MON sends FAULT → bus → other zone's HB_MON. N2 → N3: 11.42 + 11.54 + 2.43 + 11.57; N3 → N2: 11.33 + 11.57 + 2.43 + 11.54 | 37.0 / 36.9 ms | N2 → N3: **1.1 ms** max over 5 presses (photo 13, N2 B1 → N3 ERR) | 100 ms | **Meets** |
| Error propagation, zone dies silently (heartbeat path) | last HB message R + 60 ms timeout + 10 ms poll phase + HB_MON R. N2 dies: 15.0 + 60 + 10 + 11.57; N3 dies: 15.4 + 60 + 10 + 11.54 | 96.5 / 96.9 ms | not scoped (bus pulls: functional pass) | 100 ms | **Meets**, about 3 ms slack. Isolation measured: N3 in ERROR 68.4 ms max (photo 14) |

#### Why the analysis and the measurements disagree

- **Cause of the failing bounds:** the MCP2515 driver's per-frame CPU cost
  (measured 503 µs on N3, 731 µs on N2) is larger than the frame spacing
  (222 µs), and each zone listens to two buses. A worst-case burst of
  received frames (12 on N2, 20 on N3) therefore holds off every task for
  about 9–10 ms.
- **What the burst needs:** CTRL has to coincide with all three heartbeats,
  a BLINK, FAULTs from every zone and all six relayed copies. These are
  independent 20 ms streams on different clocks, so such an alignment is
  rare.
- **Why the captures missed it:** 10–15 captures per chain saw typical
  phasing, where one or two frames compete.
- **Why it still counts:** the node clocks drift relative to each other, so
  every phase alignment does eventually occur. **The measured values show
  normal behavior; they don't guarantee the deadline.** Without FAULT
  traffic (normal operation: N3 receives 14 frames per 20 ms instead of
  20), the DRIVE bound falls to 8.1 ms. That still exceeds 2 ms.

#### Remedies (not implemented)

We kept the Zephyr MCP2515 driver that the handout's design is built on.
These are the changes that would close the gap, in order of effect:

1. **Cut the driver's per-frame cost.** This is the dominant term: 30 µs
   per 3-byte SPI transaction against 4.6 µs on the wire. Options:
   - a leaner MCP2515 receive path: read `READ RX BUFFER` directly on each
     RXnIF, without re-reading CANINTF;
   - SPI DMA;
   - hardware acceptance filters, so frames a zone doesn't need never wake
     it. The Zephyr driver filters only in software.
2. **Relay only after a bus is seen degraded**, instead of always. That
   cuts 6 received frames per 20 ms at N3, but delays the first relayed frame by
   the 60 ms detection time.
3. **Pi: a priority qdisc** with CTRL in the top band, so CTRL never waits
   behind N1's own HB/BLINK/FAULT. Local blocking would drop from 810 µs
   to 0, and R_CTRL from 1.75 ms to 0.94 ms.

## Fault-injection test matrix (R4.8 a–f)

Faults were injected by pulling the jumpers between a node's CAN module and
the bus rail. Pulling them removes the node from that bus but leaves the
terminators in place.

- **Functional results** come from the full-system test on 2026-10-21. They
  were read from the zone consoles and the Pi status line, plus the LEDs.
- **Timing results** come from scope captures. Each is the maximum of the
  repeats, using the test points on the per-node breadboards. Photo numbers
  continue the scope capture log in `lab3_CAN/Progress_doc_lab3.md`.

Detection timing follows from the design. A bus is marked degraded once no
other node has been heard on it for 60 ms. HB_MON checks that every 10 ms, so
the LED lights 60–70 ms after the last frame received.

| | Fault injected | Expected: affected node | Expected: other zones | Per-bus LEDs | System state | Required timing | Measured: function | Measured: timing |
|---|---|---|---|---|---|---|---|---|
| a | One node off CAN A (demo: N2) | Marks A degraded after 60 ms of silence, keeps working on B | Keep hearing it on B, no state change | Node: DEG_A on. Others: off | Fully operational, NORMAL; no hazards, no braking | DEG_A on after 60 ms of silence (60–70 ms with the 10 ms poll) | ✅ N2 bus A pull (Part 1): N2's B unaffected, N3 still hears N1 on A and N2 on B. N1 bus A pull: Pi DEG_A on, all NORMAL | **62.8 ms, max 68.6 ms over 5** (photo 10, N2 off A: last CAN_RX_A pulse → DEG_A on) ✅ |
| b | One node off CAN B (demo: N3) | Marks B degraded after 60 ms, keeps working on A | Keep hearing it on A | Node: DEG_B on. Others: off | Fully operational, NORMAL | Same as (a) | ✅ seen for N1 inside scenario (c). Single-fault check: listed in the remaining captures | Same code path as (a) |
| c | N1 off CAN B **and** N3 off CAN A | N1 and N3 each degraded on one bus | N2 relays N1 ↔ N3 traffic (RELAY_TX toggling) | N1: DEG_B. N3: DEG_A. N2: none | Fully operational: steers, blinks, brakes through N2's relay | Degraded within 60–70 ms; brake chain 2 ms, or documented degraded latency (§3.3) | ✅ N3 NORMAL, `HB N1:-B N2:-B`, `thr 50%` → DRIVE via the relay; N2 relays about 150 frames/s, 0 dropped | Relayed brake chain **1.1 ms** max (photo 3). Relay stage 607 µs (photo 4) |
| d | One STM32 off both buses (demo: N2, all 4 wires) | Hears no heartbeats on either bus → ERROR within 100 ms. Safe state: N2 steering held (no CTRL reaches it) | Miss 3 heartbeats from N2 on both buses → system ERROR: hazards, N3 braked | N2: DEG_A and DEG_B. Others: no DEG (their buses still work) | System error | Error entry ≤ 100 ms (analysis bound 96 ms, §3.3) | ✅ N2 ERROR, both buses PASSIVE/DEG. N3 ERROR (`N2:--`), motors braked, its buses ACTIVE | N3 enters ERROR **59.4 ms, max 68.4 ms over 3** after N2 loses its last bus (photo 14) ✅. N2 enters its own fail-safe **52.4 ms, max 53.6 ms over 3** (photo 15; under 60 ms because the reference is N2's last received frame, which can follow the last heartbeat) ✅ |
| e | Whole CAN A lost (every node off A) | Each node marks A degraded | All traffic continues on B | All nodes: DEG_A on | Fully operational on B | Degraded within 60–70 ms, no functional interruption | ✅ all DEG_A on, all NORMAL on B only (`N?:-B`), CTRL gaps 36–39 ms. Needed the Pi TX watchdog fix (`mcp251x` TX queue hang) | — |
| f | Restore any pulled jumpers | Bus-off controller recovers by itself (MCP2515 automatic; Pi `restart-ms 100`). DEG clears once traffic resumes | Resume using the restored bus; an isolated node's heartbeat brings everyone back to NORMAL | Affected LEDs off | NORMAL again, no reset or power cycle | DEG off ≤ 100 ms after traffic resumes; full recovery ≤ 500 ms | ✅ every case above: all back to `N?:AB`, ACTIVE, NORMAL, with no reset | DEG_A off **9.6 ms, max 14.8 ms over 5** after bus A restored (photo 11: first CAN_RX_A pulse → DEG_A off; design ≤ 30 ms = next heartbeat + 10 ms poll) ✅. Isolation recovery, N2 back to NORMAL **3.6 ms, max 15.6 ms over 3** after its jumpers are restored, no reset (photo 16) ✅. N3 back to NORMAL **16.5 ms max over 3** (photo 17) ✅ |
