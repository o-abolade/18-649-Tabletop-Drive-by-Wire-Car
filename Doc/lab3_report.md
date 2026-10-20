
## Section 3 ## 

### Node 2 RTA

| Task | Kind | Priority level | Zephyr prio | C | T (min) | D (local) | B | R |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TEST_BTN (GPIO ISR with debounce) | ISR | Hardware | n/a | 20 µs | 50 ms | 10 ms | - | - |
| CAN_RX_A, CAN_RX_B (INT ISR + driver thread + dispatch) | Thread | 1 | 0 | 80 µs per frame | 20 ms per message | 0.3 ms | - | - |
| BLINK_F | Thread | 2 | 1 | 20 µs | 20 ms | 0.5 ms | - | - |
| STEER | Thread | 3 | 2 | 50 µs | 20 ms | 1 ms | - | - |
| HB_MON | Thread | 4 | 3 | 30 µs | 20 ms | 20 ms | - | - |
| HB_TX | Thread | 4 | 3 | 120 µs | 20 ms | 20 ms | - | - |
| FORCE_TX | Thread | 5 | 4 | 120 µs | 50 ms | 50 ms | - | - |
