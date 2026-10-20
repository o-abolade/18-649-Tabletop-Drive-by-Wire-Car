
## Section 3 ## 

### Node 2 RTA

| Task | Kind | Priority level | Zephyr prio | C | T (min) | D (local) | B | R |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TEST_BTN  | ISR | Hardware | n/a | 20 µs | 50 ms | 10 ms | 0 | 20 µs |
| CAN_RX_A, CAN_RX_B | Thread | 1 | 0 | 80 µs per frame | 20 ms per message | 0.3 ms | 20 µs | 100 µs  |
| BLINK_F | Thread | 2 | 1 | 20 µs | 20 ms | 0.5 ms | 20 µs | 120 µs  |
| STEER | Thread | 3 | 2 | 50 µs | 20 ms | 1 ms | 20 µs | 170 µs |
| HB_MON | Thread | 4 | 3 | 30 µs | 20 ms | 20 ms | 20 µs | - |
| HB_TX | Thread | 4 | 3 | 120 µs | 20 ms | 20 ms | 20 µs | - |
| FORCE_TX | Thread | 5 | 4 | 120 µs | 50 ms | 50 ms | 20 µs | - |

BLINK_F R value computed as follows:
$R_0= 20 \micro s+20 \micro s + \lceil \frac{20 \micro s}{20 ms} \rceil * 80 \micro s = 120 \micro s$, 
$R_1 = 20 \micro s+20 \micro s + \lceil \frac{120 \micro s}{20 ms} \rceil * 80 \micro s = 120 \micro s$

STEER R value computed as follows:
$R_0 = 50 \micro s + 20 \micro s + \lceil \frac{50 \micro s}{20 ms} \rceil * 80 \micro s + \lceil \frac{50 \micro s}{20 ms} \rceil * 20 \micro s = 170 \micro s$
$R_1 = 50 \micro s + 20 \micro s + \lceil \frac{170 \micro s}{20 ms} \rceil * 80 \micro s + \lceil \frac{170 \micro s}{20 ms} \rceil * 20 \micro s = 170 \micro s$

HB_MON R value computed as follows:
$R_0$
