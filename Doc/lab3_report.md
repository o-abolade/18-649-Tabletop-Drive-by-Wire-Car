
## Section 3 ## 
### Node 2 RTA (refined CAN_RX interference)

| Task | Kind | Priority level | Zephyr prio | C | T (min) | D (local) | B | R | Meets D? |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TEST_BTN (GPIO ISR with debounce) | ISR | Hardware | n/a | 20 µs | 50 ms | 10 ms | 0 | 20 µs | Yes |
| CAN_RX_A, CAN_RX_B (INT ISR + driver thread + dispatch) | Thread | 1 | 0 | 80 µs per frame | 20 ms per message | 0.3 ms | 60 µs | 160 µs | Yes (slack 140 µs) |
| BLINK_F | Thread | 2 | 1 | 20 µs | 20 ms | 0.5 ms | 60 µs | 180 µs | Yes (slack 320 µs) |
| STEER | Thread | 3 | 2 | 50 µs | 20 ms | 1 ms | 60 µs | 310 µs | Yes (slack 690 µs) |
| HB_MON | Thread | 4 | 3 | 30 µs | 20 ms | 20 ms | 60 µs | 540 µs | Yes |
| HB_TX | Thread | 4 | 3 | 120 µs | 20 ms | 20 ms | 60 µs | 540 µs | Yes |
| FORCE_TX | Thread | 5 | 4 | 120 µs | 50 ms | 50 ms | 0 | 600 µs | Yes |


#### Assumptions

- **CAN_RX interference is a count of frames, not a fixed 400 µs.** In a response window of length R, the number of CAN_RX jobs that can interfere is n(R) = min(cap, ceil(R / 222 µs)), each costing 80 µs.
  - **222 µs is the minimum spacing between frames on one bus.** It is the shortest DLC 8 frame with no stuffing, (47 + 64) bits x 2 µs, including interframe space. (The stuffed worst case, 270 µs, is the maximum frame time and is not a valid minimum spacing.)
  - **cap = 4 for tasks released by a received frame** (BLINK_F, STEER). The frame that released the task has already been processed, so only the other four message types can interfere.
  - **cap = 5 for timer-released tasks** (HB_MON, HB_TX, FORCE_TX). N2 receives CTRL, FAULT, HB_N1, HB_N3 and BLINK, each at most once per 20 ms. Check this against your hardware filters.
- **CAN_RX own response (per frame) = C + B + ISR.** Frames are at least 222 µs apart and 160 µs < 222 µs, so the thread is idle before the next frame arrives and doesn't queue behind other frames.
- **Blocking B = 60 µs.** The SPI lock is taken per transaction, one transaction is 60 µs (half of the 120 µs two-bus placeholder), and Zephyr mutex priority inheritance applies. B applies to CAN_RX, BLINK_F, STEER and HB_MON (blocked by HB_TX or FORCE_TX holding the lock) and to HB_TX (blocked by FORCE_TX). FORCE_TX is lowest, so B = 0. If one lock is held across both transactions, B = 120 µs.
- **TEST_BTN is an ISR (debounce inside it).** It is 20 µs of interference in every thread's sum, not blocking.
- **Equal-priority tasks (HB_MON and HB_TX)** don't preempt each other, so each includes the other's full C.
- **Zephyr numbering starts at 0.** Confirm the MCP2515 driver work thread priority (Kconfig) ends up at 0. Adding RELAY in Part 4 shifts everything below it down by one.
- **Single-bus analysis.** With hot/parallel (Part 4.1), frames arrive from both buses and n(R) grows. Redo it there.

#### Calculations

R_i = C_i + B_i + ISR (20) + n(R_i) * 80 + sum of the other higher-priority tasks' C. Units are µs.

- **CAN_RX:** R0 = 80 + 60 = 140. R1 = 140 + 20 (ISR) = 160. R2 = 160. Converged.
- **BLINK_F (cap 4):** R0 = 20 + 60 = 80. n = 1, so R1 = 80 + 20 + 80 = 180. n = ceil(180/222) = 1, so R2 = 180. Converged.
- **STEER (cap 4):** R0 = 50 + 60 = 110. n = 1, so R1 = 110 + 20 + 80 + 20 (BLINK_F) = 230. n = ceil(230/222) = 2, so R2 = 110 + 20 + 160 + 20 = 310. n = ceil(310/222) = 2, so R3 = 310. Converged.
- **HB_MON (cap 5):** other terms = 30 + 60 (B) + 20 (ISR) + 20 (BLINK_F) + 50 (STEER) + 120 (HB_TX) = 300. R0 = 90. n = 1, so R1 = 380. n = 2, so R2 = 460. n = 3, so R3 = 540. n = ceil(540/222) = 3, so R4 = 540. Converged.
- **HB_TX (cap 5):** other terms = 120 + 60 + 20 + 20 + 50 + 30 (HB_MON) = 300. R0 = 180. n = 1, so R1 = 380. n = 2, so R2 = 460. n = 3, so R3 = 540. Converged at 540.
- **FORCE_TX (cap 5):** other terms = 120 + 0 + 20 + 20 + 50 + 30 + 120 (HB_TX) = 360. R0 = 120. n = 1, so R1 = 440. n = 2, so R2 = 520. n = 3, so R3 = 600. n = ceil(600/222) = 3, so R4 = 600. Converged.


### Node 2 RTA (refined CAN_RX interference)

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

### Node 3 RTA (refined CAN_RX interference)

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


