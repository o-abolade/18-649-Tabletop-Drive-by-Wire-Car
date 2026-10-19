
![Power Tree Diagram](../assets/power_tree.drawio.png)

| Power Rail | Contributing Factors | Peak Current| Avg Current | Worst-Case Voltage |
| -------- | -------- | -------- | -------- | -------- |
| 3.3 Volts | STM32 (36.3 mA max, 31.1 mA typical) x 2, CAN Controller (10 mA max, 10 mA typ) x 6,  CAN Transciever (17 mA max, 10 mA typ) x 6, Yellow LED (10.8 mA max, 0 typ) x 4, Red LED (9.2 mA Max, 0 typ) x 4, self test button (0.33 mA max, 0 typ) x 3 | 317 mA | 182.2 | TBD |
| 5 Volts | L298 Logic (36 mA max, 24 typ), ACS712 (13 mA max, 10 mA typ) x2, Rasberry Pi (3 A max, 1 A typ), RED LED (9.2 mA max, 0 typ) x2| | 3.084 A | 1.044 A |
| 6-8 Volts | Row 2, Col 2 | Row 2, Col 3 | ow 1, Col 3 | ow 1, Col 3 |
| 12 Volts | Row 2, Col 2 | Row 2, Col 3 | ow 1, Col 3 | ow 1, Col 3 |


Notes:
Yellow LED current calculated as (3.3 V - 2 V) / 120 ohms = 10.8 mA

Red LED current calculated as (3.3 V - 2.2 V ) / 120 ohms = 9.2 mA

Button current calculated as 3.3 V / 10k ohms = 0.33 mA
