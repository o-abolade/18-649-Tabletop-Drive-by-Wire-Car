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

**Status guide:** Fixed by handout = mandatory; Proposed = suggested behavior; Hardware-dependent = depends on purchased or developed hardware; Design-dependent = software/interface choice; Needs calibration = must be measured or tuned.

### Driver inputs and steering

<table style="width:100%; max-width:100%; table-layout:fixed; border-collapse:collapse; font-size:7pt; line-height:1.15;">
<colgroup>
<col style="width:18%">
<col style="width:28%">
<col style="width:38%">
<col style="width:16%">
</colgroup>
<thead><tr>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Value</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Current choice / units</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Reason / hardware dependency</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Status</th>
</tr></thead>
<tbody>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Steering input</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD: min, center, max; raw units</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Read UDP values; check wheel model and direction.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Servo travel</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD: left, center, right; degrees and pulse width</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Depends on servo, linkage and chassis travel.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Steering mapping</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Monotonic; full usable steering range</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Required behavior; endpoints need calibration.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Fixed rule; values TBD</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Throttle input</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD: released/full values; raw units</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Read UDP pedal values; measure rest noise.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Throttle mapping</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Linear target speed vs pedal travel</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Starting point for S1; normalize input first.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Proposed</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Maximum speed</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD; wheel rpm or m/s</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Depends on motor, gearing, wheel size and supply.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Brake activation</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD threshold; raw or normalized units</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">S2 uses brake &gt; 0; confirm encoding and noise.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Brake priority</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Brake overrides throttle</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Required by S2; dynamic braking with PWM off.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Fixed by handout</td>
</tr>
</tbody></table>

### Turn signals, buttons and force feedback

<table style="width:100%; max-width:100%; table-layout:fixed; border-collapse:collapse; font-size:7pt; line-height:1.15;">
<colgroup>
<col style="width:18%">
<col style="width:28%">
<col style="width:38%">
<col style="width:16%">
</colgroup>
<thead><tr>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Value</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Current choice / units</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Reason / hardware dependency</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Status</th>
</tr></thead>
<tbody>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Turn thresholds</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD: left/right; steering units or degrees</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Choose from calibrated steering range.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Cancel hysteresis</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD; steering units or degrees</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">If needed, avoid noise around turn thresholds.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Proposed; calibrate</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Repeated turn press</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Same-side press leaves signal active</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Simple rule; still cancels on completed turn.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Proposed</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Both turn buttons</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Ignore conflicting request; retain turn state</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Team choice; error hazards still override.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Proposed</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Held turn button</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Act on press edge, not every UDP update</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Avoid repeated actions while held.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Proposed</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Test-button debounce</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Presses &lt;50 ms apart count as one</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">R2.5; button hardware still needs checking.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Fixed by handout</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Restore window</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Two presses within 500 ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">R3; applies to each zone&#x27;s test input.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Fixed by handout</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Force sensing</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD: current sensing or position error</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Depends on servo, available sensors and PCB.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Force mapping</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD: zero, sign, gain, deadband</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Depends on measured load and wheel response.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Force output limit</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">-100 to 100; signed value</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Front Zone description and UDP encoding.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Fixed by handout</td>
</tr>
</tbody></table>

### Drivetrain, encoders and motor limits

<table style="width:100%; max-width:100%; table-layout:fixed; border-collapse:collapse; font-size:7pt; line-height:1.15;">
<colgroup>
<col style="width:18%">
<col style="width:28%">
<col style="width:38%">
<col style="width:16%">
</colgroup>
<thead><tr>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Value</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Current choice / units</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Reason / hardware dependency</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Status</th>
</tr></thead>
<tbody>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Encoder scale</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD: counts per wheel revolution</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Encoder resolution, decoding mode and gearing.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Wheel size</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD; diameter in metres</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Needed if speed is expressed in m/s.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Speed feedback</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Average of both encoder speeds</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Rear Zone description.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Fixed by handout</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Speed-control period</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD; ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Encoder rate, MCU/RTOS budget and motor response.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Design-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Control gains</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD; units follow chosen controller</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Choose controller, then tune on actual drivetrain.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Maximum PWM</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD; % duty cycle</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Motor, driver, supply and thermal limits.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Stuck-wheel trigger</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Nonzero command; no encoder edges ≥200 ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Rear Zone description.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Fixed by handout</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Stuck-wheel increase</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD: duty step/ramp and update interval</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Increase only to chosen PWM cap; check heating.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Needs calibration</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Motor current limits</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD: open/overload thresholds; A</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Motor startup/load current and sensor accuracy.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Hardware-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Current-fault filtering</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD: persistence time; ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Reject normal transients; meet fault response needs.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Needs calibration</td>
</tr>
</tbody></table>

### CAN communication and fault handling

<table style="width:100%; max-width:100%; table-layout:fixed; border-collapse:collapse; font-size:7pt; line-height:1.15;">
<colgroup>
<col style="width:18%">
<col style="width:28%">
<col style="width:38%">
<col style="width:16%">
</colgroup>
<thead><tr>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Value</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Current choice / units</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Reason / hardware dependency</th>
<th style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#17365d; color:#fff; text-align:left;">Status</th>
</tr></thead>
<tbody>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">CAN identifiers</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Proposed in the CAN message catalog; final assignment TBD</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Assign priorities; brake/error before convenience.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Design-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Payload definitions</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Proposed in the CAN message catalog; final scaling and valid ranges TBD</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Document in CAN message catalog; sensor ranges matter.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Design-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Sequence counter</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">At least 4 bits; final width TBD</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">R4.4; define wraparound and duplicate tracking.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Fixed minimum; design TBD</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">CAN bit timing</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">500 kbit/s; timing segments TBD</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">R4.2; depends on chosen controllers and clocks.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Fixed rate; hardware TBD</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Control-message cadence</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Every UDP update; gap ≤50 ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">R4.4; receiving hardware/software must support it.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Fixed by handout</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Health indication</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">TBD: LED colors and on/off meaning</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Depends on PCB LEDs; map healthy/degraded states.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#f3f6f8;">Design-dependent</td>
</tr>
<tr>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Watchdog timeout</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">TBD; ms</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">STM32 watchdog clock, task timing and fault handling.</td>
<td style="padding:3pt; border:0.5pt solid #aab7c4; vertical-align:top; white-space:normal; overflow-wrap:anywhere; word-break:break-word; hyphens:auto; background:#fff;">Hardware/design-dependent</td>
</tr>
</tbody></table>


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
