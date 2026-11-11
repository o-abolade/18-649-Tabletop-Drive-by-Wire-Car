# Lab 5: Time Synchronization and Consensus — Progress

Handout: [Lab 5: Time Synchronization and Consensus](../Lab%20Handouts/18-449_649%20Lab%205%20-%20Time%20and%20Consensus.pdf), version 1.0. **Due November 18, 2026 at 11:59 p.m.**

## Status

- [x] Add the Lab 5 handout to the repository.
- [ ] Review the handout and resolve questions or inconsistencies with the course staff.
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
