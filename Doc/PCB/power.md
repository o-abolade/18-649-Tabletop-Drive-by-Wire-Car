
![Power Tree Diagram](../assets/power_tree.drawio.png)

| Power Rail | Contributing Factors | Peak Current| Avg Current | Worst-Case Voltage |
| -------- | -------- | -------- | -------- | -------- |
| 3.3 Volts | STM32 (160 mA max, 1uA typical) x 2, CAN Reciever (10 mA max, 1 uA typ) x 6,  CAN Transciever (17 mA max, 10 mA typ) x 6, Yellow/Red LED (30 mA max, 0 typ) x 10, self test button (50 mA max, 0 typ), Shunt Current Sense TI INA180 (300 mA max, 200 typ) | Row 1, Col 3 | ow 1, Col 3 | ow 1, Col 3 |
| 5 Volts | Row 2, Col 2 | Row 2, Col 3 | ow 1, Col 3 | ow 1, Col 3 |
| 6-8 Volts | Row 2, Col 2 | Row 2, Col 3 | ow 1, Col 3 | ow 1, Col 3 |
| 12 Volts | Row 2, Col 2 | Row 2, Col 3 | ow 1, Col 3 | ow 1, Col 3 |
