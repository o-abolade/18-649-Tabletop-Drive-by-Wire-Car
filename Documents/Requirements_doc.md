# Requirement Document

## 1. Introduction with system objective (Made by Claude can change later)

### 1.1 Background

Modern vehicles are shifting from mechanical linkages to drive-by-wire systems, where sensors, embedded controllers, and actuators replace direct mechanical connections between driver inputs and vehicle response. This gives more flexibility but removes any mechanical fallback. The industry's response has been to move toward zone architectures: functionally related sensors/actuators are grouped under a zone controller, and zones communicate over a shared, fault-tolerant bus instead of point-to-point wiring.

### 1.2 System Objective

This project implements a table-top drive-by-wire system that reflects these architectural challenges. The system must:

- Translate cockpit inputs into actuation: steering, throttle, brake, and turn-signal inputs from a force-feedback wheel/pedal set are captured by a cockpit zone and relayed to the steering and drivetrain zones to drive the physical chassis.
- Provide bi-directional steering feedback: load on the steering actuator is measured and relayed back to the wheel as force feedback, closing the loop between vehicle and driver.
- Regulate drivetrain velocity in closed loop: throttle maps to a target wheel velocity maintained via encoder feedback, with brake always taking priority.
- Communicate over dual-redundant CAN: all inter-zone traffic (control, force feedback, heartbeats, error state) is sent simultaneously on two independent buses (CAN A/B), so the system tolerates loss of either bus, or any single node's connection to a bus, with no failover delay.
- Detects and recovers from faults deterministically: zone failures (self-test, missed heartbeats) and bus faults (degraded-bus detection) are caught within bounded time windows, driving the system into a well-defined error/hazard state.

### 1.3 Scope

The system has three zones — Cockpit (Raspberry Pi), Front/Steering, and Rear/Drivetrain (independently-clocked STM32s running an RTOS). The cockpit zone bridges a UDP proxy (wheel/pedal hardware) to the CAN network; the steering and drivetrain zones consume CAN control messages and drive the servo, motors, and blinkers. A custom PCB implements dual CAN A/B interfaces per zone with fault-injection jumpers, enabling systematic bus- and node-level fault testing.

## 2. System state chart

## 3. Sequence diagrams for each use-case (S1-S6)

## 4. Traceability for each use-case (S1-S6)

## 5. Proposed timing analysis

## 6. CAN message catalog (R4.4) and bus utilization calculation (R4.6)

## 7. Table of all team-defined values 

- (turn thresholds, throttle-to-velocity mapping, force mapping, valid ranges)

## 8. Dual-CAN redundancy design

- bus topology showing both buses, termination, isolation jumpers and per-bus status LEDs on every node (R4.1, R4.3, R4.8)

## 9. Fault-injection test matrix covering scenarios (a)-(f) of R4.8
