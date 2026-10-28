
# Lab 4 STM32 pin mappings

Target: **STM32F401RET6TR (LQFP64)**. These are proposed custom-PCB
assignments chosen for PCB placement and routing. They are intentionally
allowed to differ from the working Lab 3 Nucleo overlays; a difference alone
is not a discrepancy.
Review date: **2026-10-28**. Peripheral availability checks pass as listed
below, but the unresolved items prevent schematic sign-off.

## Steering (N2)

| Pin | Use |
|-----|-----|
| PA0 | Servo current-sense ADC input: ADC1_IN0, from conditioned INA180 output |
| PA1 | Front A TX GPIO |
| PA2 | Front A RX GPIO |
| PA3 | Can B rst GPIO |
| PA4 | Can B CS GPIO|
| PA5 | SPI1 SCK (AF5) |
| PA6 | SPI1 MISO (AF5) |
| PA7 | SPI1 MOSI (AF5) |
| PA8 | - |
| PA9 | USART1 TX (AF7) |
| PA10 | USART1 RX (AF7) |
| PA11 | - |
| PA12 | - |
| PA13 | SWDIO |
| PA14 | SWCLK (debug clock, not SPI SCK) |
| PA15 | Servo PWM: TIM2_CH1 (AF1) |
| PB0 | Can A CS GPIO |
| PB1 | Can A ext interrupt  |
| PB2 | Can A deg status GPIO |
| PB3 | SWO |
| PB4 | - |
| PB5 | - |
| PB6 | - |
| PB7 | - |
| PB8 | - |
| PB9 | Blinker FL GPIO |
| PB10 | Can B deg status GPIO |
| PB12 | - |
| PB13 | - |
| PB14 | - |
| PB15 | - |
| PC0 | - |
| PC1 | - |
| PC2 | Steer self test ext interrupt |
| PC3 | - |
| PC4 | Can B external interrupt |
| PC5 | Can A rst |
| PC6 | - |
| PC7 | - |
| PC8 | - |
| PC9 | - |
| PC10 | Self test status GPIO |
| PC11 | Error status GPIO |
| PC12 | Blinker FR GPIO |
| PC13 | - |
| PC14 | Front B TX software test-point GPIO; observe PC13–PC15 output limits |
| PC15 | Front B RX software test-point GPIO; observe PC13–PC15 output limits |


## Drivetrain (N3)

| Pin | Use |
|-----|-----|
| PA0 | - |
| PA1 | Self test status GPIO |
| PA2 | Error status GPIO |
| PA3 | Can A rst GPIO |
| PA4 | Can A CS GPIO |
| PA5 | SPI1 SCK (AF5) |
| PA6 | SPI1 MISO (AF5) |
| PA7 | SPI1 MOSI (AF5) |
| PA8 | Blinker RL GPIO |
| PA9 | USART1 TX (AF7) |
| PA10 | USART1 RX (AF7) |
| PA11 | Left motor A GPIO; assumed encoder phase A input (EXTI11), confirm net |
| PA12 | Left motor B GPIO; assumed encoder phase B input (EXTI12), confirm net |
| PA13 | SWDIO |
| PA14 | SWCLK (debug clock, not SPI SCK) |
| PA15 | Motor ENA PWM: TIM2_CH1 (AF1) |
| PB0 | Can B CS GPIO |
| PB1 | Can B ext interrupt  |
| PB2 | Can B deg sts GPIO |
| PB3 | SWO |
| PB4 | - |
| PB5 | Motor in 4 GPIO |
| PB6 | Motor ENB PWM: TIM4_CH1 (AF2) |
| PB7 | Motor in 3 GPIO |
| PB8 | - |
| PB9 | - |
| PB10 | Can A deg status GPIO |
| PB12 | Rear A RX GPIO |
| PB13 | Rear A TX GPIO |
| PB14 | Rear B TX GPIO |
| PB15 | Rear B RX GPIO |
| PC0 | Left motor current-sense ADC input: ADC1_IN10 |
| PC1 | Right motor current-sense ADC input: ADC1_IN11 |
| PC2 | Drivetrain self-test button input (EXTI2) |
| PC3 | Overload GPIO |
| PC4 |  Can A external interrupt |
| PC5 |  Can B rst GPIO|
| PC6 | Right motor A GPIO; candidate encoder phase A (EXTI6), duplicate label on PC13 |
| PC7 | Right motor B GPIO; candidate encoder phase B (EXTI7), duplicate label on PC14 |
| PC8 | - |
| PC9 | - |
| PC10 | - |
| PC11 | Motor In 1 GPIO |
| PC12 | Motor in 2 GPIO |
| PC13 | Right motor A GPIO; candidate encoder phase A (EXTI13), duplicate label on PC6 |
| PC14 | Right motor B GPIO; candidate encoder phase B (EXTI14), duplicate label on PC7 |
| PC15 | Blinker RR GPIO; direct LED-source drive requires reassignment or a buffer |

## Functional review

The following checks use the [STM32F401xD/xE datasheet, pin definitions and
alternate-function table](https://www.st.com/resource/en/datasheet/stm32f401re.pdf)
and the local Zephyr `stm32f401r(d-e)tx-pinctrl.dtsi` definitions.

| Function | Check | Result / required configuration |
|---|---|---|
| Shared CAN SPI | PA5/PA6/PA7 support SPI1 SCK/MISO/MOSI at AF5 on both MCUs | Valid. Each MCP2515 needs a separate active-low GPIO CS; PA4 and PB0 are valid GPIO CS choices. Use software CS rather than assigning PA4 as hardware SPI1_NSS. |
| CAN interrupts | N2 PB1 (A), PC4 (B); N3 PC4 (A), PB1 (B) | Valid EXTI1 and EXTI4 inputs. The A/B assignments are intentionally reversed between zones; the PCB-specific CAN definitions must match. Configure active-low MCP2515 INT with an appropriate pull-up. |
| Self-test | PC2 on each MCU | Valid EXTI2, separate from both CAN interrupt lines. Match button polarity and debounce to the circuit. |
| Servo PWM | N2 PA15 | Valid TIM2_CH1/AF1. Configure a 20 ms period and a suitable timer prescaler. Reconfigure the reset-default JTDI function while retaining SWD on PA13/PA14. |
| Motor PWM | N3 PA15 and PB6 | Valid TIM2_CH1/AF1 and TIM4_CH1/AF2. Independent timers permit matching the existing 100 us motor PWM period. PB6 is a normal timer output; the current Lab 3 complementary TIM1_CH1N configuration must not be reused. |
| UART | PA9/PA10 | Valid USART1 TX/RX at AF7. Connect TX to the adapter RX and RX to adapter TX, using 3.3 V logic and a common ground. Current Nucleo console uses USART2, so its configuration must change. |
| Debug | PA13/PA14 and optional PB3 | Valid SWDIO/SWCLK and SWO. Expose NRST, GND and target-voltage reference on the debug connector too. |
| Current ADC | N2 PA0; N3 PC0/PC1 | Valid ADC1 channels 0, 10 and 11. Use analog mode and ensure the conditioned inputs stay within VSSA–VDDA; analog mode is not 5 V tolerant. Verify ACS712 output conditioning and INA180 output range in the schematic. |
| Encoders (assuming the motor A/B GPIO labels mean encoder phases) | Left PA11/PA12; right PC6/PC7 **or** PC13/PC14 | Each candidate pair can be GPIO interrupt inputs. Identify the intended right pair. With either pair, encoder EXTI lines do not collide with CAN EXTI1/4 or button EXTI2. The same line number on another port cannot simultaneously be another EXTI source. |
| CAN TX/RX test points | FRONT_*/REAR_* GPIOs | These are software timing outputs, not the MCP2515/SN65HVD230 physical TXCAN/RXCAN nets. Label and configure them accordingly. |

## Unresolved schematic decisions

1. **Right motor A/B labels duplicated:** the original map lists these on both
   PC6/PC7 and PC13/PC14. The review assumes these mean encoder phases because
   the H-bridge direction pins are separately named IN1–IN4. Confirm that
   interpretation and the connector nets in Fusion, then identify the intended
   right pair or rename any different signals. PC6/PC7 are a valid encoder
   option; no physical pin reassignment has been made by this review.
2. **Rear-right blinker on PC15:** ST limits PC13–PC15 output speed to
   2 MHz with at most 30 pF load and prohibits using these pins as current
   sources such as direct LED drivers (pin-definition footnote 2). If RR is an
   active-high LED driven directly from the MCU, move it to a free ordinary
   GPIO such as PC8, or use an appropriately designed external buffer. PC8 is
   currently unused in this proposed N3 map; it is only a suggestion until
   Fusion is updated. A sinking configuration still needs its current and
   total PC13–PC15 supply-switch budget checked.
3. **Front diagnostic outputs on PC14/PC15:** low-load software test points
   can use these pins only within the same speed/load/current limits. Keep
   LSE disabled if PC14/PC15 are used as GPIOs, and account for scope-probe
   capacitance. PB6/PB7 are free alternatives in the proposed N2 map if the
   probe/load limits cannot be met.
4. **Schematic verification remains open:** no editable Fusion schematic or
   exported netlist is in this repository. MCU capability checks cannot verify
   the routed connector nets, LED drive circuit, reset pull-ups, power pins,
   BOOT0, ADC conditioning, or connector polarity. Export the schematic/netlist
   and resolve the items above before treating this as a verified PCB pin map.

## Firmware porting notes

Different Lab 4 pin assignments are expected and are not a hardware error.
Create PCB-specific devicetree definitions for CS/INT, lamps, software test
points, PWM, encoders, USART1 and the ADCs. Add any MCP2515 reset/status
outputs used by the PCB. Keep the Lab 3 Nucleo definitions for the existing
bench setup.

The Nucleo clock tree currently expects an 8 MHz ST-Link clock on HSE.
Configure the custom PCB's actual external clock/crystal or internal HSI;
ST-Link's Nucleo clock cannot be assumed on the custom PCB. Also configure
TIM2 (servo on N2; ENA on N3) and TIM4 (ENB on N3) with their own periods.
