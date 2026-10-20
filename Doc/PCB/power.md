
![Power Tree Diagram](../assets/power_tree.drawio.png)

| Power Rail | Contributing Factors | Peak Current| Avg Current | Worst-Case Voltage |
| -------- | -------- | -------- | -------- | -------- |
| 3.3 Volts | STM32 (36.3 mA max, 31.1 mA typical) x 2, CAN Controller (10 mA max, 10 mA typ) x 6,  CAN Transciever (17 mA max, 10 mA typ) x 6, Yellow LED (10.8 mA max, 0 typ) x 4, Red LED (9.2 mA Max, 0 typ) x 4, self test button (0.33 mA max, 0 typ) x 3 | 317 mA | 182.2 mA | TBD |
| 5 Volts | L298 Logic (36 mA max, 24 typ), ACS712 (13 mA max, 10 mA typ) x2, Rasberry Pi (3 A max, 1 A typ), RED LED (9.2 mA max, 0 typ) x2| 3.084 A | 1.044 A | - |
| 7 Volts | Steering Servo (3 A max, 100 mA typ) | 3 A | 0.1 A | - |
| 12 Volts | Motor (3.2 A max, 0.36 typ) x 2, 3V3 Buck Converter (100 mA max, 55 mA typ), 5V Buck Converter (1.35 A max, 0.457 A typ), 7 V Buck Converter (1.94 A max, 65 mA typ) | 9.79 A | 1.297 A | 9.063 |


Notes:
Yellow LED current calculated as (3.3 V - 2 V) / 120 ohms = 10.8 mA

Red LED current calculated as (3.3 V - 2.2 V ) / 120 ohms = 9.2 mA

Button current calculated as 3.3 V / 10k ohms = 0.33 mA

For the buck regulators, input current given by I_in = (Vout * Iout) / (efficiency * Vin)
90% efficiency estimated for 7V regulator, since no chart on the data sheet

Estimating 30 mOhm internal resistance for NiMH batteries, max voltage drop is 9.79 A * 10 batteries * 30 mOhm / battery = 2.937 Volts

# Lab 4 power design todo

## Power budget
- [ ] Determine encoder power draw (datasheet or measure) and its supply voltage and output levels, then assign a rail
- [ ] Add the buck enable-divider currents (about 0.4 mA each) and feedback-divider currents to the budget
- [ ] Add the L298 VS quiescent current (70 mA max) to the 12 V rail
- [ ] Fix the 12 V average (both motors: about 1.30 A before the L298 quiescent current)
- [ ] Add a 1 mA shunt-amplifier placeholder and about 2 mA of pull-ups to 3V3
- [ ] Add 20-30% margin to each rail
- [ ] Recompute converter input currents at the sagged battery voltage
- [ ] Fill in the worst-case voltage column for all four rails
- [ ] Measure stall currents (both motors and the servo) and the Pi's peak draw

## Battery
- [ ] Select a NiMH cell, then check its rated continuous discharge current and internal resistance
- [ ] Determine the minimum battery voltage at which every rail regulates (the 7 V rail sets it)
- [ ] Decide cell count and servo voltage (7 V vs 6-6.5 V) based on the sag result
- [ ] Pick a holder and check lead length and connector

## Input protection
- [ ] Fuse (about 1.5x peak, roughly 15-17 A)
- [ ] Reverse-polarity circuit (check Vds rating and RDS(on) heat at 10 A)
- [ ] 470-1000 uF bulk capacitor on the motor rail
- [ ] Connector choice, and trace width or pours for about 10 A

## Buck converters (TPS564252)
- [ ] 3V3: inductor, caps, feedback resistors
- [ ] 5 V: inductor (about 4.7 uH), caps, feedback resistors
- [ ] 7 V: inductor (about 6.8 uH), 44 uF or more at 16 V or higher, 107 k / 10 k feedback
- [ ] Enable dividers (about 20 k over 10 k) for the battery-fed bucks
- [ ] Input capacitors for each buck

## Motor drive and sensing
- [ ] L298 overload protection and thermal plan (3.2 A stall exceeds the 2 A DC rating)
- [ ] Servo shunt: pick the amplifier, then size the shunt and gain for about 3 A stall, plus the RC filter and test point
- [ ] ACS712 output divider to keep the signal at or below 3.3 V

## Brief, review, and deadlines
- [ ] Chassis measurements, outline, and standoff size
- [ ] Power tree figure, budget table, and battery paragraph (label every number measured or datasheet)
- [ ] **Wed Oct 21:** BoM to TA (part number, manufacturer, quantity plus spares, distributor)
- [ ] Design review with TA (this week)
- [ ] **Thu Oct 29, 11:59 pm:** final Gradescope submission

## Done
- [x] Rail assignments and power tree diagram
- [x] Buck selection (TPS564252 for all three rails)
- [x] Button and LED circuit decisions
- [x] First-pass 3V3, 5 V, 7 V, and 12 V budgets
