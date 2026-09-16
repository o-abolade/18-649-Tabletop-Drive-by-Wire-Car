**Misho Alexandrov (mvalexan), Ore Abolade (oabolade), T'sairus Beasley (tbeasley)**

## Unit Testing Document

This document defines planned tests and expected results for the tabletop drive-by-wire system. Record measured results only after each test is run.

---

## 1. Test scope and traceability

Lab 1 requires testing of the blinker subsystem, one additional subsystem, and CAN redundancy using the fault-injection jumpers. This plan uses drivetrain control as the additional subsystem. It also includes self-test and recovery because they affect system safety.

- **Blinker subsystem:** left/right indication, automatic cancel, timing, synchronization and hazards.
- **Drivetrain control:** throttle mapping, closed-loop speed control, brake priority, stuck-wheel behavior and invalid inputs.
- **Self-test and recovery:** startup, LOCAL_FAIL, SYSTEM_FAIL, debounce, heartbeat loss and restoration.
- **CAN redundancy:** dual transmission, duplicate suppression, degradation, isolation and recovery.

---

## 2. Test setup and evidence

Run tests in unit-test mode where possible. Use simulated UDP state updates and CAN-frame injection or logging to create repeatable inputs. Record the software build, hardware revision, message IDs and team-defined values used for each run.

- **Oscilloscope:** measure end-to-end time from the cockpit UDP-receive GPIO edge to the output test point. Use it for motor PWM, dynamic braking, servo, blinkers and test-button timing.
- **USB-CAN adapter or CAN analyzer:** capture CAN A and CAN B separately. Record IDs, payloads, sequence counters, timestamps and bus load.
- **Section 8 setup check:** before CAN-02 through CAN-07, confirm each node's jumper labels and locations, CAN test-header pinout, status-LED meaning, termination configuration and CAN-analyzer connection point against the final Dual-CAN topology.

---

## 3. Blinker subsystem tests

Run the tests below with documented left/right thresholds. Capture front and rear blinker outputs on separate oscilloscope channels for timing and synchronization checks.

| Test ID | Precondition and input | Expected result | Requirements |
|---|---|---|---|
| BL-01 | Normal state. Left signal is off; wheel is not past the left threshold. Press left-turn button. | Front and rear left blinkers start within 100 ms. Right blinkers stay off. | S4; R2.4 |
| BL-02 | Left signal is active. Turn wheel past the left threshold, then return past it. | Left front and rear blinkers stop. No right blinker activates. | S4 |
| BL-03 | Normal state. Right signal is off; wheel is not past the right threshold. Press right-turn button. | Front and rear right blinkers start within 100 ms. Left blinkers stay off. | S4; R2.4 |
| BL-04 | Right signal is active. Turn wheel past the right threshold, then return past it. | Right front and rear blinkers stop. No left blinker activates. | S4 |
| BL-05 | Left signal is active. Press right-turn button. | Right signal becomes active; left signal stops. | S4 |
| BL-06 | Normal state. Capture front and rear outputs for one active turn signal. | Blink period is 0.9–1.1 Hz with equal on/off time. Same-side front/rear edges differ by no more than 1 ms. | R2.4 |
| BL-07 | Trigger SYSTEM_FAIL from a valid normal state. | All four blinkers flash together at 2 Hz ±10%, with equal on/off time. Normal turn state is overridden. | S4; R3 |
| BL-08 | Send an out-of-range steering, throttle, brake or turn-state value in unit-test mode. | System enters SYSTEM_FAIL and hazards activate. | R3 |

---

## 4. Drivetrain-control tests

Document the throttle-to-speed mapping, maximum speed, encoder scale, PWM limit and current thresholds before running these tests.

| Test ID | Precondition and input | Expected result | Requirements |
|---|---|---|---|
| DR-01 | Brake released. Send a low, mid and full throttle command. | Target speed increases monotonically with throttle. Full throttle does not exceed the documented maximum speed. | S1; Rear Zone |
| DR-02 | Brake released. Command a steady target speed; record both encoder speeds and PWM. | Average encoder speed settles near the target and the controller adjusts PWM to hold it. | S1; Rear Zone |
| DR-03 | Throttle is nonzero. Send a brake command. | PWM is disabled and dynamic braking is applied within 2 ms of cockpit UDP reception. | S2; R2.2 |
| DR-04 | Brake remains active while throttle changes. | Brake remains active; throttle cannot re-enable motor drive. | S2 |
| DR-05 | Command nonzero speed while preventing encoder transitions for at least 200 ms. | Controller increases PWM only up to the documented maximum. Record current and temperature limits during this test. | Rear Zone |
| DR-06 | Send an invalid throttle, brake or speed-control value. | System enters SYSTEM_FAIL: PWM is disabled and dynamic braking is applied. | R3 |

---

## 5. Self-test and system-state tests

Use the System state chart terms INIT, NORMAL, LOCAL_FAIL and SYSTEM_FAIL. Run each test once per zone unless a test explicitly names one zone.

| Test ID | Precondition and input | Expected result | Requirements |
|---|---|---|---|
| ST-01 | System starts powered off, then powers on. | Every zone begins in INIT with safe outputs. It reaches NORMAL only after required peer heartbeats and valid STM32 control data are received. | R3 |
| ST-02 | Normal state. Single-press the cockpit, steering or drivetrain test input. | Pressed zone reaches LOCAL_FAIL within 10 ms, stops heartbeats on both buses and sets safe outputs. | R2.5; R3; R4.5 |
| ST-03 | One zone is in LOCAL_FAIL; monitor both other zones. | Other zones enter SYSTEM_FAIL within 100 ms of the press. Hazards flash and drivetrain brakes. | R2.5; R3 |
| ST-04 | Normal state. Generate two test-input presses less than 50 ms apart. | Treat the presses as one press; do not restore a failed zone. | R2.5 |
| ST-05 | A zone is in LOCAL_FAIL. Send two presses within 500 ms. | Zone resumes heartbeats. System returns to NORMAL within 500 ms after heartbeats from all zones are received again. | R3 |
| ST-06 | Cockpit receives no UDP state update for 100 ms. | Cockpit stops its heartbeat until UDP updates resume. Other zones then follow the normal missed-heartbeat failure path. | Cockpit Zone; R4.5 |

---

## 6. CAN-redundancy tests

Run CAN-02 through CAN-07 using the final Section 8 topology. Confirm the installed isolation jumpers, LED meanings and analyzer connections. These tests must be repeated for all applicable nodes and bus combinations.

| Test ID | Fault action | Expected result | Requirements |
|---|---|---|---|
| CAN-01 | Capture normal traffic on CAN A and CAN B. | Every catalog message appears on both buses with the same sequence counter. Receiver acts on the first valid copy and ignores the duplicate. | R4.4; R4.7 |
| CAN-02 | Isolate one node from CAN A only using its Section 8 jumper. | All functions continue with R2 timing. Affected CAN A connection is marked degraded by LED and heartbeat; no hazards or braking. | R4.7; R4.8(a) |
| CAN-03 | Isolate one node from CAN B only. | Same expected result as CAN-02, for CAN B. | R4.7; R4.8(b) |
| CAN-04 | Isolate two different nodes from opposite buses. | All three zones remain functional. Each affected connection shows degraded status. | R4.8(c) |
| CAN-05 | Isolate one node from both buses. | Other zones enter SYSTEM_FAIL within 100 ms. Isolated node enters its local safe state. | R2.5; R4.8(d) |
| CAN-06 | Disconnect every node from the same bus. | System continues normally on the remaining bus. Affected bus connections show degraded status. | R4.7; R4.8(e) |
| CAN-07 | Restore any removed jumper(s). | Degraded indication clears within 100 ms of traffic returning. Normal operation resumes within 500 ms without reset or power cycle. | R4.7; R4.8(f) |
| CAN-08 | Cause or simulate CAN controller bus-off, then restore the bus. | Controller recovers automatically within 100 ms of bus restoration. | R4.8 |

---

## 7. Test-result record

Create one record for each executed test. Link the raw oscilloscope capture, CAN trace, log file or photo that supports the result.

| Test ID | Run date | Build / hardware version | Evidence location | Pass / fail | Notes or measured result |
|---|---|---|---|---|---|
| *Add one row per executed test.* | | | | | |

---

## 8. Test completion rules

- A test passes only when every expected result is observed and its timing requirement is met.
- If a test fails, record the observed behavior, measured timing, build/hardware version and follow-up issue before rerunning it.
- Update the traceability section in the Requirements Document with the final test ID and evidence link after verification.
