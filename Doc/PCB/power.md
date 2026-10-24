
![Power Tree Diagram](../assets/power_tree.drawio.png)

| Power Rail | Contributing Factors | Peak Current| Avg Current | Worst-Case Voltage |
| -------- | -------- | -------- | -------- | -------- |
| 3.3 Volts | STM32 (36.3 mA max, 31.1 mA typical) x 2, CAN Controller (10 mA max, 10 mA typ) x 6,  CAN Transciever (17 mA max, 10 mA typ) x 6, Yellow LED (10.8 mA max, 0 typ) x 4, Red LED (9.2 mA Max, 0 typ) x 8, self test button (0.33 mA max, 0 typ) x 5, INA180 (260 uA max, 197 uA typ), Motor Encoded (estimated 25 mA max, typ) x2 | 403.31 mA | 232.34 mA | 3.3 V |
| 5 Volts | L298 Logic (36 mA max, 24 typ), ACS712 (13 mA max, 10 mA typ) x2, Rasberry Pi (3 A max, 1 A typ), RED LED (22.3 mA max, 0 typ) x4 | 3.151 A | 1.044 A | 5 V |
| 6.8 Volts | Steering Servo (3 A max, 100 mA typ) | 3 A | 0.1 A | 6.8 V |
| 12 Volts | Motor (3.2 A max, 0.36 typ) x 2, 3V3 Buck Converter (120.6 mA max, 71.0 mA typ), 5V Buck Converter (1.382 A max, 0.472 A typ), 6V8 V Buck Converter (1.89 A max, 64.3 mA typ) | 9.79 A | 1.327 A | 7.105 V |


Notes:
**Selected battery source:** Tenergy **11616**, a 10S1P Sub-C NiMH battery
pack: 12 V nominal, 4.2 Ah (50.4 Wh), 30 A maximum continuous discharge,
113 x 91 x 23 mm, 1.55 lb, with a Standard Tamiya connector.  The 30 A
rating provides margin over the calculated 9.79 A peak system load.  It is an
off-board part, but is included in the project BOM.  The board-side power
input connector must mate with its Standard Tamiya connector (or use a
documented Tamiya-to-board harness); do not select an XT60 footprint unless
the battery harness is changed accordingly.

Yellow LED current calculated as (3.3 V - 2 V) / 120 ohms = 10.8 mA

Red LED current for STM32s calculated as (3.3 V - 2.2 V ) / 120 ohms = 9.2 mA

Button current calculated as 3.3 V / 10k ohms = 0.33 mA

For the buck regulators, input current given by I_in = (Vout * Iout) / (efficiency * Vin)
90% efficiency estimated for 6V8 regulator at 3A, 88% at typical current, based on data other diagrams since no chart for 6V8 on the data sheet

For the initial sag calculation, NiMH cells are conservatively modeled as
50 mOhms per cell.  For ten series cells, the worst case voltage is
$12 - 9.79 * 10 * .050 = 7.105 V$.  Measure the selected pack's voltage at
load before treating this as a final value.
According to the datasheet, the buck regulator has a maximum duty cycle of 97%, meaning that the minimum input voltage needed for the lines to regulate are $V_{in} = 6.8/0.97 = 7.01 V$ for the 6.8 volt line, $V_{in} = 5 / 0.97 = 5.15 V$ for the 5 volt line, and $V_{in} = 3.3 / 0.97 = 3.40 V$ for the 3.3 volt line. Thus, at the sagged voltage, all lines should still regulate.


## Implemented input-power protection

The selected 12 V source is the Tenergy 11616 NiMH pack. The implemented
schematic path is:

`Tenergy Standard-Tamiya harness -> J4 (TE 282837-2) -> F1 -> Q1 -> +12V_PROTECTED`

- **J4:** TE 282837-2 two-position, 13.5 A terminal block. The battery's
  Standard-Tamiya lead terminates in a documented Tamiya-to-bare-wire harness
  at J4; it is not an XT60 connection.
- **F1:** Littelfuse 0215010.MXEP, a 10 A time-delay, through-hole axial fuse.
  It is close to J4 and protects against sustained input faults while allowing
  short motor-start/stall transients.
- **Q1:** IRF4905PBF P-channel MOSFET in the high side for reverse-polarity
  protection. Its source is on the fused battery side and its drain creates
  `+12V_PROTECTED`. Its maximum RDS(on) is 20 mOhm at VGS = -10 V; at 10 A it
  can dissipate about 2 W, so its copper area and temperature must be checked
  during bring-up.
- **R59:** 100 kOhm gate-to-ground pull-down. It turns Q1 on for correct
  polarity and keeps it off for a reverse battery connection.
- **D17:** TZM5242B-GS08, a 12 V Zener connected from Q1 gate to source
  (cathode at source, anode at gate) to limit negative VGS during transients.
  Fusion's generic SOD-80 symbol is retained, but the BOM identifies the
  correct 12 V member rather than the library's 5.1 V default.
- **C47:** 1000 uF, 25 V polarized electrolytic from `+12V_PROTECTED` to GND,
  connected at the L298 VS motor-supply net. It must be placed near the L298
  VS/GND pins in layout.
- The three buck-converter inputs and L298 VS use `+12V_PROTECTED`; no former
  `12V` input net may bypass F1 or Q1.
- The 5 A SK520B Schottkys remain flyback diodes on the L298 outputs and are
  **not** in series with the battery input.


## NOTES

- X5R ceramics like the ones we're using on buck inputs lose capacitance under DC bias, so we need to have more than 2 on the 5V and 7V rails to maintain the same effective capacitance (Claude estimates 40% loss on the 7V line, 25% on the 5V)
- 7 V rail is now 6.8 Volts

## TODO
- [ ] Figure out how to change silkscreen labels on connectors so that it's easy to write things on the board
- [ ] Need to take another look at jumpers
- [X] Add input protection circuit
- [ ] Double check SWD
- [X] Determine 12 V rail power source
- [ ] Somebody needs to take a look at what I did for power sourcing on the STM32s (all the VDD, AVDD, VCAP, etc.) and what I did for Boot0 (pull down resistor)
- [ ] Add test points
- [ ] Get mechanical measurements of board, add to schematic
- [ ] Double check the right packaging for LEDs was selected

