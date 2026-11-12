# Lab 5: Time Synchronization and Consensus — Progress

Handout: [Lab 5: Time Synchronization and Consensus](../Lab%20Handouts/18-449_649%20Lab%205%20-%20Time%20and%20Consensus.pdf), version 1.0. **Due November 18, 2026 at 11:59 p.m.**

## Status

- [x] Add the Lab 5 handout to the repository.
- [x] Review the handout; questions for the course staff are listed under "Questions for the course staff" below.
- [ ] Resolve those questions with the course staff.
- [ ] Choose the architecture, task ownership, interfaces, and integration plan.
- [ ] Implement and test the system and complete the report and checkoff.

## Implementation checklist

### 1. Node setup and Raft clients

- [ ] Set up a Raft agent, log, configuration and client on each node.
- [ ] Implement client updates, network-state queries and follower-to-leader redirection.

### 2. Leader election

- [ ] Implement the identical cluster configuration and predefined-leader startup in term 1.
- [ ] Implement leader heartbeats, missed-heartbeat detection, randomized election timeouts and majority voting.
- [ ] Test leader failure and recovery without unnecessary elections for isolated link failures.

Handout defaults: three nodes, cockpit default leader, 20 ms heartbeat, three missed heartbeats, 0–39 ms election jitter and 30,000 log entries. Confirm how all startup modes in the checkoff map to the configuration.

### 3. Replicated log

- [ ] Implement term, index and event fields, replication, commit and recovery.
- [ ] Record cockpit state, node up/down and each node's individual CAN A/B link up/down events.
- [ ] Verify consistent ordering and recovery after node and link failures.

### 4. Raft state machine

- [ ] Implement log-applied network graph mutations and `getNetwork()`.
- [ ] Verify each node converges on the same committed network state.

### 5. Routing and relaying

- [ ] Derive routing tables from each node's committed network state.
- [ ] Handle multi-link failure and information propagation, including relayed traffic and recovery.
- [ ] Confirm the handout's Part 5 checkpoint and replay behavior against the final design.

### 6. Time synchronization

- [ ] Implement leader reference messages, follower global time and local drift correction.
- [ ] Coordinate blinker timing and leader handover; measure the timing and drift bounds required by the handout.

### 7. Demo and checkoff

- [ ] Prepare leader-election, log-integrity and synchronization demonstrations.
- [ ] Run the handout's cold-start, traffic, blinker, link-fault, relay, leader-failure, rejoin, random-leader and isolation scenarios.
- [ ] Capture scope evidence: FL/RL synchronization, failure response, leader handover and relayed synchronization.
- [ ] Review the handout's Redis/GUI requirements and their applicability with the course staff; implement and demonstrate as required.

## Report and submission

- [ ] Submit the report to Gradescope with the GitHub link.
- [ ] Explain drift detection and adjustment (maximum one page).
- [ ] Describe the Raft state machine (maximum one page).
- [ ] Explain client functionality and CAN routing during multi-link failures (maximum one page).
- [ ] Prepare every team member for checkoff questions.

## Decisions, measurements and open questions

Record the owner, evidence and completion date beside each item as work progresses. Timing results and hardware behavior are not yet verified for Lab 5.

**Hardware:** the Lab 3 setup (Pi + two Nucleos, six XIAO CAN modules,
blinker/DEG/ERR LEDs, test-point boards). The Lab 4 PCB has not arrived.

### Questions for the course staff

The checkoff sequence uses names and behaviour that the handout body does not
define. Each question lists the working assumption we use until it is
answered.

1. **Undefined message names.** The checkoff refers to `REF`, `APPEND`,
   `REQ_VOTE`, `MEMBER` and `EVENT`, but Parts 1–6 define none of them. Is
   there a message catalog for Lab 5, or do we define our own?
   *Assumption:* we define the catalog: `REF` = leader heartbeat carrying the
   master time and a cycle counter, `APPEND` = log replication,
   `REQ_VOTE` = vote request, `MEMBER` = a committed network-state entry
   (NODE / CANLINK / COCKPIT), `EVENT` = a client report sent to the leader.
2. **Undefined LEDs and probe points.** The checkoff expects `LEADER` and
   `SYNC` indicators and scopes `HZ` and `REF_RX`. What should each show?
   *Assumption:* `LEADER` lit on the current leader; `SYNC` lit while a
   follower holds a valid global time (always on for the leader); `HZ` =
   hazard lamp output; `REF_RX` = test point pulsed when a `REF` is received.
3. **Heartbeat vs. REF period.** Part 2 says the 20 ms leader heartbeat *is*
   the clock-sync message. Checkoff step 2 expects `REF` every 20 ms *and*
   an empty `APPEND` every 100 ms. Are these two messages? Which one resets a
   follower's election timeout?
   *Assumption:* `REF` every 20 ms is the Raft heartbeat and resets the
   election timeout (`heartbeat_ms` = 20); the leader also sends an empty
   `APPEND` every 100 ms to carry the commit index when nothing new is
   appended.
4. **Failure-mode hazard rate.** The error-state hazard rate is already
   defined: 2 Hz ±10 %, 50 % duty (Lab 1 S4, Lab 2 §3.4), and our Lab 3 code
   uses it. Steps 6–7 add a 4 Hz rate in failure mode that "returns to 2 Hz"
   when the Pi is back. Is 4 Hz a new Lab 5 requirement for the cockpit-lost
   failure mode only?
   *Assumption:* yes: 4 Hz while `COCKPIT_ERROR` is committed, 2 Hz in every
   other error state.
5. **Log entry types.** Part 3 defines `COCKPIT_*`, `NODE_X_*` and
   `CANLINK_X_Y_*`. The checkoff counts "`MEMBER` entries" and an "`EVENT`
   from N2 reaching N3's log via the Pi". Are these the same entries? What
   event does N2 send?
   *Assumption:* both are names for the Part 3 entries; the N2 `EVENT` is a
   `CANLINK` report it sends to the leader, which then commits it to every
   log.
6. **Held events and replay.** Step 7 expects "held events" to appear on the
   Pi's display once, in order and marked replayed. Does "held events" mean
   the entries committed while the Pi was down, and what is "the display"
   (the cockpit console?)?
   *Assumption:* the new leader keeps every entry committed during the
   outage; when the Pi rejoins, normal Raft log catch-up sends them, and the
   cockpit console prints them once, marked `replayed`, before live entries.
7. **Startup modes.** The demo uses "default mode" and "leader setup mode";
   the checkoff uses `LEADER=cockpit` and `LEADER=random` with a seed. How do
   these map to `default_leader`?
   *Assumption:* default / `LEADER=cockpit` = `default_leader: cockpit`;
   leader-setup / `LEADER=random` = `default_leader: none`, a term-1 election
   with the jitter seeded from a configurable `seed`, so one seed value makes
   the Pi win.
8. **`log_size` = 30000 on the STM32s.** The F401RE has 96 KB of RAM and
   about 65 KB is free in our Lab 3 builds; 30 000 entries need 60–120 KB
   even when packed. Is log compaction (a snapshot of the network graph plus
   a ring buffer of recent entries) acceptable, or may `log_size` be smaller
   on the Nucleos?
   *Assumption:* `log_size` stays a config key; the STM32s keep a ring buffer
   of recent entries plus a snapshot of the applied state.
9. **"Config file" on the STM32s.** The Nucleos have no filesystem. Is a
   shared, compile-time config header (identical on all three nodes, read by
   the Pi too) acceptable?
   *Assumption:* yes; one source file defines the keys, and the Pi build uses
   the same file.
10. **"The Part 5 checkpoint".** The extra credit says to "run the Part 5
    checkpoint", but Part 5 has no checkpoint. Which scenario is meant?
    *Assumption:* checkoff steps 6–7 (Pi down, Pi back).
11. **Network delay estimate.** Part 6.1 says TTCAN uses "the properties of
    the CAN bus" to estimate latency. Is a fixed delay (frame time at
    500 kbit/s plus measured driver latency) enough, or do we need to
    timestamp the actual transmit time (e.g. a follow-up frame carrying the
    previous `REF`'s send time)?
    *Assumption:* the leader timestamps `REF` at transmit completion and
    sends that time in the next `REF`; followers add a fixed delay measured
    on the scope.
12. **Relayed sync delay.** Step 9 syncs a cut-off STM32 through a relayed
    `REF`. Should the follower correct for the extra relay hop?
    *Assumption:* yes. Relays keep forwarding frames unchanged (a "relayed"
    flag caused same-ID collisions in Lab 3), so the follower uses its routing
    table: if the leader has no link on the bus the `REF` arrived on, the
    `REF` was relayed and the follower adds the measured relay delay.
