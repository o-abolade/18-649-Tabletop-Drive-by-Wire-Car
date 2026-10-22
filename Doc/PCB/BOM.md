| Item  | Amount | Purpose | Datasheet | Common Part ? | JLCPBP Part ? | Reasoning, if not |
| -------- | -------- | -------- | -------- | -------- | -------- | -------- |
| TPS564252DRLR | 3 | Buck Converter for 6V8, 5 V, and 3.3 V rails | [Datasheet](https://www.ti.com/lit/ds/symlink/tps564252.pdf?ts=1695219253429&ref_url=https%253A%252F%252Fwww.ti.com%252Fsitesearch%252Fen-us%252Fdocs%252Funiversalsearch.tsp%253FlangPref%253Den-US%2526searchTerm%253DTPS564252DRLR%2526nr%253D8) | Y | - | - |
| MCP2515T-I/ST | 6 | CAN Controller | [Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Stand-Alone-CAN-Controller-with-SPI-20001801J.pdf) | Y | - | - |
| SN65HVD230MDREP | 6 | CAN Transceiver | [Datasheet](https://www.ti.com/general/docs/suppproductinfo.tsp?distId=10&gotoUrl=https%3A%2F%2Fwww.ti.com%2Flit%2Fgpn%2Fsn65hvd230m-ep) | Y | - | - |
| L298P | 1 | Full Bridge Driver | [Datasheet](https://www.st.com/content/ccc/resource/technical/document/datasheet/82/cc/3f/39/0a/29/4d/f0/CD00000240.pdf/files/CD00000240.pdf/jcr:content/translations/en.CD00000240.pdf) | Y | - | - |
| STM32F401RET6TR | 2 | STM 32 Controller | In Docs | Y | - | - |
| ACS712ELCTR-05B-T | 2 | Motor Current Sense | [Datasheet](https://www.allegromicro.com/~/media/Files/Datasheets/ACS712-Datasheet.ashx) | Y | - | - |
| 151031YS06000 | 4 | Turn Signal LEDs | [Datasheet](https://www.we-online.com/components/products/datasheet/151031YS06000.pdf) | Y | - | - |
| 151031SS06000 | 12 | Bus Status LEDs (x6), Err LED (x3), status LED (x3) | [Datasheet](https://www.we-online.com/components/products/datasheet/151031SS06000.pdf) | Y | - | - |
| SWT0210-045026GSA | 5 | Self Test Buttons (x3), STM32 NRST (x2) | [Datasheet](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8937/swt0210-spec.pdf) | Y | - | - |
| CX3225SB16000H0FLJCC| 6 | Oscillators for CAN Controllers | [Datasheet](https://ele.kyocera.com/assets/products/crystal-device/cx3225sb_e.pdf) | Y | - | - |
| RC0603FR-07120RL | 20 | 120 ohm, LED series resistance (x4 for blinkers, x12 status LEDs), CAN Bus Termination (x4) | [Datasheet](https://yageogroup.com/content/datasheet/asset/file/PYU-RC_GROUP_51_ROHS_L) | Y | - | - |
| CGA3E2X7R1E104K080AA | 21 | 100 nF, RC debounce on self test buttons (3x), RC debounce on NRST (x2), input/output decoupling on buck converters (6x), VDD smoothing for STM32s (x8), STM32 ADC input smoothing (x2) | [Datasheet](https://product.tdk.com/system/files/dam/doc/product/capacitor/ceramic/mlcc/catalog/mlcc_automotive_general_en.pdf) | Y | - | - |
| CRCW060310K0FKEA | 30 | 10 Kohm, Self Test Buttons Pull Up Resistor (3x), NRST Button Pull Up (2x), Buck Converter Enable Voltage Dividers (3x), Buck Converter PG Output (3x), 3V3 buck converter feedback, 5V feedback circuit (4x), 8V8 feedback circuit (3x), CAN transciever RS pins (6x), STM32 BOOT0 pin pull down (x2)  | [Datasheet](https://www.vishay.com/docs/20035/dcrcwe3.pdf) | Y | - | - |
| RC0603FR-0747KL | 3 | 47 Kohm, 3V3 buck converter feedback, motor sense voltage divider (x2)  | [Datasheet](https://yageogroup.com/content/datasheet/asset/file/PYU-RC_GROUP_51_ROHS_L) | Y | - | - |
| IHLP2525CZER3R3M11 | 1 | 3.3 uH inductor on 3v3 buck converter output | [Datasheet](https://www.vishay.com/docs/34190/ihlp-2525cz-11.pdf) | N | N | Not available on parts list, equivalent JLCPCB parts either required bulk purchase or had no CAD model |
| IHLP2525CZER4R7MA1 | 1 | 4.7 uH inductor on 5V buck converter output | [Datasheet](https://www.vishay.com/docs/34241/ihlp-2525cz-a1.pdf) | N | N | See above |
| IHLP2525CZEK6R8MA1 | 1 | 6.8 uH inductor on 6V8 buck converter output | [Digikey Page](https://www.digikey.com/en/products/detail/vishay-dale/IHLP2525CZEK6R8MA1/26235892) | N | N | See above |
| GRM219R61C226ME15L | 8 | Output filtering on buck converters  | [Digikey Page](https://www.digikey.com/en/products/detail/murata-electronics/GRM219R61C226ME15L/4905420?s=N4IgTCBcDaIOICUCyYCMBOBA2VBhMYWSAoqgKwAyIAugL5A) | Y | - | - |
| CRCW0603210KFKEA | 2 | 210 kOhm, 5V and 6V8 converter feedback circuit | [Digikey page](https://www.digikey.com/en/products/detail/vishay-dale/CRCW0603210KFKEA/1174930) | Y | - | - |
| RMCF0603FT91K0 | 1 | 91 kOhm, 6V8 converter feedback circuit | [Digikey page](https://www.digikey.com/en/products/detail/stackpole-electronics-inc/RMCF0603FT91K0/1714297) | Y | - | - |
| INA180A1IDBVT | 1 | Current sense on steering | [Digikey Page](https://www.digikey.com/en/products/detail/texas-instruments/INA180A1IDBVT/7219063) | N | N | CAD models available for this one |
| CSNL2512FTL500 | 1 | Shunt Resistor | [Digikey Page](https://www.digikey.com/en/products/detail/stackpole-electronics-inc/CSNL2512FTL500/1788105) | N | N | JLCPCB's available current sense resistors had far too high a resistance value |
| ERJ-3EKF1001V  | 2 | RC filtering on steering ADC input | [Digikey Page](https://www.digikey.com/en/products/detail/panasonic-industry/ERJ-3EKF1001V/196032?s=N4IgTCBcDaIKICUBSBaAzHA0gMQIwAZ9cA1EAXQF8g) | Y | - | - |
| GRM21BR71C475KE51L | 1 | See above | [Digikey Page](https://www.digikey.com/en/products/detail/murata-electronics/GRM21BR71C475KE51L/6606095?s=N4IgTCBcDaIOICUCyYCMAhBB2VBhALFgKwDSAokagDIgC6AvkA) | Y | - | - |
| C0603C220J1GACTU | 12 | Crystal Oscillator Load Capacitance | [Digikey Page](https://www.digikey.com/en/products/detail/kemet/C0603C220J1GACTU/2200183) | Y | - | - |
| 742863160 | 2 | Ferrite beads, AVDD filtering on STM32s | [Digikey Page](https://www.digikey.com/en/products/detail/w%C3%BCrth-elektronik/742863160/2268591?s=N4IgTCBcDaIOwBYwA4BsBmAjKgDCAugL5A) | Y | - | - |
| GRM188R71A225KE15D | 2 | VCAP on stm32s | [Digikey Page](https://www.digikey.com/en/products/detail/murata-electronics/GRM188R71A225KE15D/1033256) | N | N | JLCPCB CAD not compatible with Fusion 360 |
| 2.54 mm pin headers | 40 | Steering input (x3), Steering SWD pins (x6), Steering STM uart (x3), CAN jumpers (x12), Drivetrain SWD pins (x6), drivetrain STM uart (x3), RPI connection (x7) | N/A | Y | - | - |
| B6B-PH-SM4-TB | 2 | Motor connection (x2) | Y | - | - |
| B3B-PH-SM4-TB | 1 | Steering Servo connection | N | N | Same product line as B6B conn |
| GCM1885C2A102FA16D | 2 | Motor current sense filter | Y | - | - |
