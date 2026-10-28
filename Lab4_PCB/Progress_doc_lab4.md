# Lab 4 PCB Progress Report

## Current status

- Power-rail budget, battery selection, mechanical envelope, current sensing,
  and the system block diagram are documented in [brief.md](brief.md).
- The schematic BOM is maintained in [BOM.md](BOM.md).
- Input-power protection is documented in [power.md](power.md): 12 V NiMH
  pack, 10 A time-delay fuse, reverse-polarity P-channel MOSFET, gate clamp,
  and bulk capacitance at the L298 motor supply.
- The planned test-point names are listed in [TestPoints.md](TestPoints.md).
- The proposed MCU assignments and the 2026-10-28 functional review are in
  [pin_mappings.md](pin_mappings.md). SPI, UART, PWM, ADC and candidate EXTI
  assignments are supported by the STM32F401RE; the map is not ready for
  schematic sign-off until the issues below are resolved.

## Completed

- [x] Select a 12 V source: Tenergy 11616, 10S1P NiMH, 4.2 Ah, 30 A maximum
  continuous discharge.
- [x] Calculate the preliminary 12 V peak load: 9.79 A.
- [x] Add protected input path: battery input -> 10 A time-delay fuse ->
  P-channel reverse-polarity MOSFET -> `+12V_PROTECTED`.
- [x] Route the three buck converters and L298 motor supply from
  `+12V_PROTECTED`.
- [x] Add C47, 1000 uF / 25 V, at the L298 motor-supply net.
- [x] Define the initial test-point plan.

## Remaining work

- [ ] Resolve the two right-encoder pin pairs (PC6/PC7 versus PC13/PC14).
- [ ] Resolve N3 PC15 rear-right LED drive and verify N2 PC14/PC15 diagnostic
  output loading against ST's special GPIO restrictions.
- [ ] Export the Fusion schematic/netlist and reconcile connector nets with
  the pin map; create PCB-specific firmware pin/ADC/clock configuration.
- [ ] Resolve the motor-connector current rating and the L298 current-limit
  margin before ordering the board.
- [ ] Confirm footprints, especially the radial C47 lead spacing and the
  input connector/fuse footprints.
- [ ] Complete PCB placement and routing, including wide high-current traces
  and thermal copper for Q1.
- [ ] Add and label all test points and board silkscreen labels.
- [ ] Run ERC/DRC and reconcile the Fusion-generated BOM with [BOM.md](BOM.md).
- [ ] Measure battery voltage sag, MOSFET temperature, rail regulation, and
  motor/servo current during hardware bring-up.
