
## Section 3 ## 

### Node 2 RTA 

| Task | Kind | Priority level | Zephyr prio | C | T (min) | D (local) | B | R | Meets D? |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TEST_BTN (GPIO ISR with debounce) | ISR | Hardware | n/a | 20 µs | 50 ms | 10 ms | 0 | 20 µs | Yes |
| CAN_RX_A, CAN_RX_B (INT ISR + driver thread + dispatch) | Thread | 1 | 0 | 80 µs per frame | 20 ms per message | 0.3 ms | 60 µs | 160 µs | Yes (slack 140 µs) |
| BLINK_F | Thread | 2 | 1 | 20 µs | 20 ms | 0.5 ms | 60 µs | 180 µs | Yes (slack 320 µs) |
| STEER | Thread | 3 | 2 | 50 µs | 20 ms | 1 ms | 60 µs | 310 µs | Yes (slack 690 µs) |
| HB_MON | Thread | 4 | 3 | 30 µs | 20 ms | 20 ms | 60 µs | 540 µs | Yes |
| HB_TX | Thread | 4 | 3 | 120 µs | 20 ms | 20 ms | 60 µs | 540 µs | Yes |
| FORCE_TX | Thread | 5 | 4 | 120 µs | 50 ms | 50 ms | 0 | 600 µs | Yes |

C, T and D are the handout's placeholders. Replace C with measured values.

#### Assumptions

- **Bus Interference** In a response window of length R, the number of CAN_RX jobs that can interfere is n(R) = min(cap, ceil(R / 222 µs)), each costing 80 µs.
  - **222 µs is the minimum spacing between frames on one bus.** It is the shortest DLC 8 frame with no stuffing, (47 + 64) bits x 2 µs, including interframe space. (The stuffed worst case, 270 µs, is the maximum frame time and is not a valid minimum spacing.)
  - **cap = 4 for tasks released by a received frame** (BLINK_F, STEER). The frame that released the task has already been processed, so only the other four message types can interfere.
  - **cap = 5 for timer-released tasks** (HB_MON, HB_TX, FORCE_TX). N2 receives CTRL, FAULT, HB_N1, HB_N3 and BLINK, each at most once per 20 ms. Check this against your hardware filters.
- **CAN_RX own response (per frame) = C + B + ISR.** Frames are at least 222 µs apart and 160 µs < 222 µs, so the thread is idle before the next frame arrives and doesn't queue behind other frames.
- **Blocking B = 60 µs.** The SPI lock is taken per transaction, one transaction is 60 µs (half of the 120 µs two-bus placeholder), and Zephyr mutex priority inheritance applies. B applies to CAN_RX, BLINK_F, STEER and HB_MON (blocked by HB_TX or FORCE_TX holding the lock) and to HB_TX (blocked by FORCE_TX). FORCE_TX is lowest, so B = 0. If one lock is held across both transactions, B = 120 µs.
- **TEST_BTN is an ISR (debounce inside it).** It is 20 µs of interference in every thread's sum, not blocking.
- **Equal-priority tasks (HB_MON and HB_TX)** don't preempt each other, so each includes the other's full C.
- **Zephyr numbering starts at 0.** Confirm the MCP2515 driver work thread priority (Kconfig) ends up at 0

#### Calculations

R_i = C_i + B_i + ISR (20) + n(R_i) * 80 + sum of the other higher-priority tasks' C. Units are µs.

- **CAN_RX:** R0 = 80 + 60 = 140. R1 = 140 + 20 (ISR) = 160. R2 = 160. Converged.
- **BLINK_F (cap 4):** R0 = 20 + 60 = 80. n = 1, so R1 = 80 + 20 + 80 = 180. n = ceil(180/222) = 1, so R2 = 180. Converged.
- **STEER (cap 4):** R0 = 50 + 60 = 110. n = 1, so R1 = 110 + 20 + 80 + 20 (BLINK_F) = 230. n = ceil(230/222) = 2, so R2 = 110 + 20 + 160 + 20 = 310. n = ceil(310/222) = 2, so R3 = 310. Converged.
- **HB_MON (cap 5):** other terms = 30 + 60 (B) + 20 (ISR) + 20 (BLINK_F) + 50 (STEER) + 120 (HB_TX) = 300. R0 = 90. n = 1, so R1 = 380. n = 2, so R2 = 460. n = 3, so R3 = 540. n = ceil(540/222) = 3, so R4 = 540. Converged.
- **HB_TX (cap 5):** other terms = 120 + 60 + 20 + 20 + 50 + 30 (HB_MON) = 300. R0 = 180. n = 1, so R1 = 380. n = 2, so R2 = 460. n = 3, so R3 = 540. Converged at 540.
- **FORCE_TX (cap 5):** other terms = 120 + 0 + 20 + 20 + 50 + 30 + 120 (HB_TX) = 360. R0 = 120. n = 1, so R1 = 440. n = 2, so R2 = 520. n = 3, so R3 = 600. n = ceil(600/222) = 3, so R4 = 600. Converged.

### Node 3 RTA 

| Task | Kind | Priority level | Zephyr prio | C | T (min) | D (local) | B | R | Meets D? |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TEST_BTN (GPIO ISR with debounce) | ISR | Hardware | n/a | 20 µs | 50 ms | 10 ms | 0 | 20 µs | Yes |
| CAN_RX_A, CAN_RX_B (INT ISR + driver thread + dispatch) | Thread | 1 | 0 | 80 µs per frame | 20 ms per message | 0.3 ms | 60 µs | 160 µs | Yes (slack 140 µs) |
| DRIVE | Thread | 2 | 1 | 50 µs | 20 ms | 0.5 ms | 60 µs | 210 µs | Yes (slack 290 µs) |
| BLINK_R | Thread | 3 | 2 | 20 µs | 20 ms | 0.5 ms | 60 µs | 310 µs | Yes (slack 190 µs) |
| PID | Thread | 4 | 3 | 200 µs | 10 ms | 10 ms | 60 µs | 590 µs | Yes |
| HB_MON | Thread | 5 | 4 | 30 µs | 20 ms | 20 ms | 0 | 760 µs | Yes |
| HB_TX | Thread | 5 | 4 | 120 µs | 20 ms | 20 ms | 0 | 760 µs | Yes |

Same assumptions apply as for RTA for Node 2

#### Calculations

R_i = C_i + B_i + ISR (20) + n(R_i) * 80 + sum of the other higher-priority tasks' C. Units are µs.

- **CAN_RX:** R0 = 80 + 60 = 140. R1 = 140 + 20 (ISR) = 160. R2 = 160. Converged.
- **DRIVE (cap 4):** R0 = 50 + 60 = 110. n = 1, so R1 = 110 + 20 + 80 = 210. n = ceil(210/222) = 1, so R2 = 210. Converged.
- **BLINK_R (cap 4):** R0 = 20 + 60 = 80. n = 1, so R1 = 80 + 20 + 80 + 50 (DRIVE) = 230. n = ceil(230/222) = 2, so R2 = 80 + 20 + 160 + 50 = 310. n = ceil(310/222) = 2, so R3 = 310. Converged.
- **PID (cap 5):** other terms = 200 + 60 (B) + 20 (ISR) + 50 (DRIVE) + 20 (BLINK_R) = 350. R0 = 260. n = ceil(260/222) = 2, so R1 = 510. n = 3, so R2 = 590. n = ceil(590/222) = 3, so R3 = 590. Converged.
- **HB_MON (cap 5):** other terms = 30 + 0 (B) + 20 + 50 + 20 + 200 (PID) + 120 (HB_TX) = 440. R0 = 30. n = 1, so R1 = 520. n = 3, so R2 = 680. n = 4, so R3 = 760. n = ceil(760/222) = 4, so R4 = 760. Converged.
- **HB_TX (cap 5):** other terms = 120 + 0 + 20 + 50 + 20 + 200 + 30 (HB_MON) = 440. R0 = 120. n = 1, so R1 = 520. n = 3, so R2 = 680. n = 4, so R3 = 760. Converged at 760.

### 3.2 CAN message response times

This is the single-bus case.  Every catalog message has DLC 8, so the
worst-case transmission time is 270 us at 500 kbit/s (including worst-case
bit stuffing).  CAN arbitration is fixed-priority and non-preemptive: a
smaller identifier has higher priority, and a message can be blocked once by
one lower-priority frame already on the wire.

The release jitter `J` is the maximum delay from a message's trigger until
its sender queues it.  The STM32 values below are the response times from
Section 3.1.  `J_PI = 300 us` is an **assumption for this draft**, not a
measurement; it must be replaced by the maximum scoped `UDP_RX`-to-`CMD_TX`
latency before checkoff.  The FAULT row uses the conservative maximum of the
possible sender jitters, 300 us.

For a message `m`, the queuing delay is:

`w_m = B_m + sum(ceil((w_m + J_j + 2 us) / T_j) * C_j)` for every
higher-priority message `j`, and `R_m = J_m + w_m + C_m`.

All response windows are below 2.5 ms, so every higher-priority 20 ms or
50 ms message can arrive at most once in each window.  Thus each table value
converges after the first iteration.  `B = 270 us` for every row except
FORCE, which has no lower-priority message and therefore has `B = 0`.

| ID | Message | T (min) | J | C | B | w | R | D | Meets D? |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0x010 | CTRL | 20 ms | 300 us assumed (`J_PI`) | 270 us | 270 us | 270 us | 840 us | 2 ms | Yes |
| 0x020 | FAULT | 20 ms | 300 us assumed (max sender) | 270 us | 270 us | 540 us | 1.11 ms | 100 ms | Yes |
| 0x100 | HB_N1 | 20 ms | 300 us assumed (`J_PI`) | 270 us | 270 us | 810 us | 1.38 ms | 20 ms | Yes |
| 0x101 | HB_N2 | 20 ms | 540 us (N2 HB_TX R) | 270 us | 270 us | 1.08 ms | 1.89 ms | 20 ms | Yes |
| 0x102 | HB_N3 | 20 ms | 760 us (N3 HB_TX R) | 270 us | 270 us | 1.35 ms | 2.38 ms | 20 ms | Yes |
| 0x200 | BLINK | 20 ms | 300 us assumed (`J_PI`) | 270 us | 270 us | 1.62 ms | 2.19 ms | 100 ms | Yes |
| 0x300 | FORCE | 50 ms | 600 us (N2 FORCE_TX R) | 270 us | 0 us | 1.62 ms | 2.49 ms | 50 ms | Yes |

At the specified minimum interarrivals, one bus has six 20 ms frames
(CTRL, FAULT, three heartbeats, and BLINK) and one 50 ms FORCE frame:

`U = 6 * 270 us / 20 ms + 270 us / 50 ms = 8.64%`.

This is below R4.6's 50% per-bus utilization limit.  These results assume
FAULT and BLINK are rate-limited to their catalog 20 ms minimum, and that the
MCP2515/driver does not introduce an additional multi-frame local-TX queue.
The latter must be checked on hardware because the implemented driver exposes
one transmit buffer at a time.

### 3.3 End-to-end chains

The following chains use the Section 3.1 node response times and the
single-bus message responses above.  Values remain an analysis using the
handout WCET placeholders.  In particular, the 300 us Pi stage is assumed;
the final report must replace it with the maximum measured Pi latency under
the stated test load.

| Chain | Worst-case path and calculation | Bound | Deadline | Verdict |
| --- | --- | ---: | ---: | --- |
| Brake / throttle | `UDP_RX -> CTRL_TX` and CTRL bus response: `R_CTRL = 840 us`; N3 `CAN_RX = 160 us`; N3 `DRIVE = 210 us`. `840 + 160 + 210` | 1.21 ms | 2 ms | Meets, with 790 us slack under the assumed Pi value |
| Steering | `R_CTRL = 840 us`; N2 `CAN_RX = 160 us`; N2 `STEER = 310 us`. `840 + 160 + 310` | 1.31 ms | 50 ms | Meets |
| Blinker start (front) | `R_BLINK = 2.19 ms`; N2 `CAN_RX = 160 us`; N2 `BLINK_F = 180 us`. `2.19 + 0.160 + 0.180` | 2.53 ms | 100 ms | Meets |
| Blinker start (rear) | `R_BLINK = 2.19 ms`; N3 `CAN_RX = 160 us`; N3 `BLINK_R = 310 us`. `2.19 + 0.160 + 0.310` | 2.66 ms | 100 ms | Meets |
| Blinker synchronization | A shared BLINK frame arrives at both STM32s at the same bus instant.  The local receive-to-edge bounds are N2: `160 + 180 = 340 us`; N3: `160 + 310 = 470 us`.  With no lower bound other than zero, the conservative edge offset is `max(340, 470)`. | 470 us | 1 ms | Meets, with 530 us slack |
| Error propagation | N2 `TEST_BTN` local fail: 20 us; three 20 ms heartbeat periods: 60 ms; worst phase to the next 20 ms `HB_MON` poll: 20 ms; N3 `HB_MON` response: 760 us. `0.020 + 60 + 20 + 0.760` | 80.78 ms | 100 ms | Meets |

The brake chain is the limiting normal-operation chain.  Its STM32 and CAN
portion is 910 us, leaving at most 1.09 ms for the measured Pi
`UDP_RX`-to-CTRL-queue stage.  If the measured maximum exceeds 1.09 ms, the
single-bus brake requirement is not met; the remedy is to bound the Pi path
(for example, pinned `SCHED_FIFO` execution and a measured load test) or
reduce the receiving-node/CAN path.  The error-chain bound includes the
worst-case heartbeat-monitor polling phase; it assumes the local fail handler
suppresses heartbeat transmission immediately.


