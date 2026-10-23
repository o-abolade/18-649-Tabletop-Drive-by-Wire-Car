# Lab 4 Test Points

Signal names are placeholders (the Lab 3 test-point table is not in the Lab 4 document). Rename them to match your schematic nets.

## Grounds
- G$1: GND 
- G$2: GND
- G$3: GND
- G$4: GND 

## Power rails
- G$5: 12V
- G$6: 6V8
- G$7: 5V
- G$8: 3V3

## Power-good
- G$9: 3V3_PG
- G$10: 5V_PG
- G$11: 6V8_PG

## PWM
- G$12: PWM_L (rear left motor)
- G$13: PWM_R (rear right motor)
- G$14: SERVO_PWM (steering)

## Blinkers
- G$15: BLINK_FL
- G$16: BLINK_FR
- G$17: BLINK_RL
- G$18: BLINK_RR

## Self-test buttons
- G$19: SELFTEST_REAR
- G$20: SELFTEST_FRONT
- G$21: SELFTEST_PI

## Error state / ERR outputs
- G$22: ERR_REAR
- G$23: ERR_FRONT
- G$24: ERR_PI

## UDP
- G$25: UDP_RX_GPIO

## CAN TX/RX (per bus, per zone)
- G$26: REAR_A_TX
- G$27: REAR_A_RX
- G$28: REAR_B_TX
- G$29: REAR_B_RX
- G$30: FRONT_A_TX
- G$31: FRONT_A_RX
- G$32: FRONT_B_TX
- G$33: FRONT_B_RX
- G$34: COCKPIT_A_TX
- G$35: COCKPIT_A_RX
- G$36: COCKPIT_B_TX
- G$37: COCKPIT_B_RX

## Current sensing
- G$38: I_L_ADC (ACS712 left motor output)
- G$39: I_R_ADC (ACS712 right motor output)
- G$40: OVERLOAD (overload GPIO)
- G$41: V_SHUNT_RAW (unfiltered servo shunt ADC input)

## Notes
- If the shunt circuit uses a differential ADC input, use two test points (e.g. V_SHUNT_P and V_SHUNT_N) instead of one.
- The Lab 3 table asks for two ground pins per group, so add extra grounds near each cluster of test points.
- DEG_A and DEG_B LEDs (R4.7) are not required test points in Section 1.4, but can be added if there is room.
- TA-checked test points must be 2.54 mm headers or Keystone 5015 hooks, and labeled in silkscreen.
