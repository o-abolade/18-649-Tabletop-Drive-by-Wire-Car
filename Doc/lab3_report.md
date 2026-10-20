
## Section 3 ## 

### Node 2 RTA

| Task | Kind | Priority level | Zephyr prio | C | T (min) | D (local) | B | R | Meets D? |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TEST_BTN (GPIO ISR with debounce) | ISR | Hardware | n/a | 20 µs | 50 ms | 10 ms | 0 | 20 µs | Yes |
| CAN_RX_A, CAN_RX_B (INT ISR + driver thread + dispatch) | Thread | 1 | 0 | 80 µs per frame | 20 ms per message | 0.3 ms | 60 µs | 160 µs | Yes (slack 140 µs) |
| BLINK_F | Thread | 2 | 1 | 20 µs | 20 ms | 0.5 ms | 60 µs | 500 µs | Yes (slack 0 µs) |
| STEER | Thread | 3 | 2 | 50 µs | 20 ms | 1 ms | 60 µs | 550 µs | Yes (slack 450 µs) |
| HB_MON | Thread | 4 | 3 | 30 µs | 20 ms | 20 ms | 60 µs | 700 µs | Yes |
| HB_TX | Thread | 4 | 3 | 120 µs | 20 ms | 20 ms | 60 µs | 700 µs | Yes |
| FORCE_TX | Thread | 5 | 4 | 120 µs | 50 ms | 50 ms | 0 | 760 µs | Yes |

C, T and D are the handout's placeholders. Replace C with measured values.

#### Assumptions

- **CAN_RX interference = 5 × 80 µs = 400 µs.** N2 receives CTRL, FAULT, HB_N1, HB_N3 and BLINK (not FORCE, not its own HB_N2). Each is modeled as its own sporadic source with T = 20 ms, and all five can land inside one response window. Verify this against your actual hardware filters.
- **CAN_RX own response (per frame) = C + B + ISR.** Frames on one bus are at least 270 µs apart, and 160 µs < 270 µs, so the driver thread is idle before the next frame and doesn't queue behind other frames.
- **Blocking B = 60 µs.** The SPI lock is taken per transaction, one transaction is half of the 120 µs two-bus placeholder, and Zephyr mutex priority inheritance applies. B applies to CAN_RX, BLINK_F, STEER and HB_MON (blocked by HB_TX or FORCE_TX holding the lock), and to HB_TX (blocked by FORCE_TX). FORCE_TX is lowest, so B = 0. If one lock is held across both transactions, B = 120 µs and every row grows by 60 µs.
- **TEST_BTN is an ISR (debounce inside it).** It appears as 20 µs of interference in every thread's sum, not as blocking.
- **Equal-priority tasks (HB_MON and HB_TX)** don't preempt each other, so each includes the other's full C.
- **Zephyr numbering starts at 0.** Confirm the MCP2515 driver work thread priority (Kconfig) ends up at 0. Adding RELAY in Part 4 shifts everything below it down by one.
- All ceiling terms are 1 because every R is far below every period.

#### Calculations

R_i = C_i + B_i + sum over hp(i) of ceil(R_i / T_j) * C_j. Units are µs. ISR = TEST_BTN = 20, RX = CAN_RX aggregate = 400.

- **CAN_RX:** R0 = 80 + 60 = 140. R1 = 140 + 20 (ISR) = 160. R2 = 160. Converged.
- **BLINK_F:** R0 = 20 + 60 = 80. R1 = 80 + 20 (ISR) + 400 (RX) = 500. R2 = 500. Converged.
- **STEER:** R0 = 50 + 60 = 110. R1 = 110 + 20 + 400 + 20 (BLINK_F) = 550. R2 = 550. Converged.
- **HB_MON:** R0 = 30 + 60 = 90. R1 = 90 + 20 + 400 + 20 (BLINK_F) + 50 (STEER) + 120 (HB_TX) = 700. R2 = 700. Converged.
- **HB_TX:** R0 = 120 + 60 = 180. R1 = 180 + 20 + 400 + 20 + 50 + 30 (HB_MON) = 700. R2 = 700. Converged.
- **FORCE_TX:** R0 = 120 + 0 = 120. R1 = 120 + 20 + 400 + 20 + 50 + 30 + 120 (HB_TX) = 760. R2 = 760. Converged.

#### Notes for the report

- BLINK_F meets its 0.5 ms deadline with zero slack under these assumptions. That is a result worth stating. A refinement is that the BLINK frame's own CAN_RX job finishes before BLINK_F is released, so only the other four messages interfere (320 µs), giving R = 420 µs.
- Each sender's R becomes the release jitter J for its message in 3.2 (for example, HB_TX's 700 µs is J for 0x101).
