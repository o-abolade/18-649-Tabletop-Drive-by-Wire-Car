<!-- Print layout for Markdown Preview Enhanced / Prince PDF export. -->
<link rel="stylesheet" href="pdf-export.css" media="print">

# 18-649 Lab 2: Report Document

**Misho Alexandrov (mvalexan), Ore Abolade (oabolade), T'sairus Beasley (tbeasley)**

---

## System Diagram
![Top Level Block Diagram](assets/Lab2_Block_Diagram.png)

## Circuit Diagram
![Top Level Block Diagram](assets/lab2_circuit_diagram.png)

## Team-Defined Values

## Task Table

## Integration Writeup

In the final design, the Raspberry Pi continues to perform the same functions as in Lab 2. It receives the UDP wheel stream from the laptop proxy, decodes it, sends commands to the microcontroller at least every 50 ms, and returns a status frame every 20 ms. The single STM32 is divided into two zones connected by a physical CAN bus. The steering zone takes over the steering servo, the servo current sensor, and the front blinkers (FL and FR). The drivetrain zone takes over motor driving, the motor current sensors, the encoders, and the rear blinkers (RL and RR). We assume the Pi's UART terminates on the steering zone, which acts as a gateway: it translates Pi commands into CAN frames and collects CAN status data for the Pi's status frame.

### Messages and Arbitration

Each in-memory variable in the Lab 2 firmware that must cross a zone boundary becomes a CAN message, as summarized in Table 1. The identifiers shown are proposed values.

**Table 1. Proposed CAN message catalog**

<table style="width:100%; max-width:100%; table-layout:fixed; border-collapse:collapse; font-size:7pt; line-height:1.15;">
<colgroup>
<col style="width:15%">
<col style="width:8%">
<col style="width:18%">
<col style="width:27%">
<col style="width:20%">
<col style="width:12%">
</colgroup>
<thead><tr>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Message</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Proposed ID</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Sender → receiver(s)</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Payload layout</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Rate / trigger</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Priority reason</th>
</tr></thead>
<tbody>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">SYSTEM_ERROR</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">0x080</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Any zone → all zones</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">B0: version + seq[3:0]; B1: source zone; B2: error reason; B3: state; B4–B7: reserved</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">On SYSTEM_FAIL entry; repeat ≤50 ms while active</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Highest: commands safe outputs.</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">VEHICLE_CONTROL</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">0x100</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Cockpit → steering, drivetrain</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">B0: version + seq[3:0]; B1: throttle 0–100%; B2: brake 0–100%; B3–B4: steering −1000…+1000; B5: turn state; B6: control flags; B7: reserved</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Every UDP update; gap ≤50 ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">High: carries brake and current driver control.</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">HEARTBEAT_COCKPIT</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">0x300</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Cockpit → steering, drivetrain</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">B0: version + seq[3:0]; B1: INIT/NORMAL/LOCAL_FAIL/SYSTEM_FAIL; B2: CAN A/B health flags; B3: error code; B4–B7: reserved</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Every 20 ms ±10% while the zone is healthy</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needed for failure detection.</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">HEARTBEAT_STEERING</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">0x301</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Steering → cockpit, drivetrain</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Same layout as HEARTBEAT_COCKPIT</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Every 20 ms ±10% while the zone is healthy</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Needed for failure detection.</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">HEARTBEAT_DRIVETRAIN</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">0x302</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Drivetrain → cockpit, steering</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Same layout as HEARTBEAT_COCKPIT</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Every 20 ms ±10% while the zone is healthy</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needed for failure detection.</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">FORCE_FEEDBACK</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">0x400</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Steering → cockpit</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">B0: version + seq[3:0]; B1: signed force −100…+100; B2: load status; B3–B7: reserved</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">At least every 50 ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Convenience feedback; below safety messages.</td>
</tr>
</tbody></table>


Because CAN arbitration favors the lower identifier, identifier order doubles as priority order. The error-state message is assigned the lowest ID, followed by brake, steering, throttle, and blinker messages, with status and heartbeat frames last. This ordering reflects the deadlines in our requirements: the brake path has a 2 ms budget, steering has 50 ms, and blinkers have 100 ms. A brake command should never wait behind any lower-priority frame other than one already in transmission.

### Redundant Buses

The system uses two CAN buses that carry identical traffic. Every message is transmitted on both buses, so each controller receives every frame twice and must discard the duplicate. Each frame carries a sequence counter; a controller accepts the first copy it observes and drops the second, which ensures that a command is never applied twice and that either bus alone is sufficient to operate the vehicle.

### Bus Health Monitoring

Each controller must also monitor the health of both buses and detect degradation, not only total loss. For each bus, a controller tracks the time since the last received frame relative to the expected message rate, whether frames are arriving on one bus but not the other, and the CAN error counters and bus-off events. A bus that falls behind its counterpart is marked degraded, and this status is included in the status frame so that it is visible to the Pi. If both buses fall silent, or a zone stops sending heartbeats, the system enters the error state: the drivetrain zone brakes the motors, the steering zone holds position, and all four blinkers flash together as hazards.

### Blinker Synchronization

In Lab 2, the front and rear blinkers shared a single MCU and time base. After the split, each half runs on a separate MCU with its own oscillator, so independent 1 Hz timers will gradually drift apart and may violate the 1 ms front/rear synchronization requirement. One zone must therefore own the blink timebase and broadcast BLINK_SYNC, and the other zone must correct its local timer when each message arrives. Because bus latency varies with arbitration, this correction carries a jitter bound that we will need to measure. If synchronization messages stop arriving, each zone continues to free-run and flags the loss.
