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

- [x] Set up a Raft agent, log, configuration and client on each node (step 4: N1, N2, N3 on hardware).
- [x] Implement client updates, network-state queries and follower-to-leader redirection (step 4).

### 2. Leader election

- [x] Implement the identical cluster configuration and predefined-leader startup in term 1 (step 1–2; host-tested).
- [x] Implement leader heartbeats, missed-heartbeat detection, randomized election timeouts and majority voting (step 2, with pre-vote and check-quorum; host-tested).
- [ ] Test leader failure and recovery without unnecessary elections for isolated link failures (passes in the simulator; hardware still to do).

Handout defaults: three nodes, cockpit default leader, 20 ms heartbeat, three missed heartbeats, 0–39 ms election jitter and 30,000 log entries. Confirm how all startup modes in the checkoff map to the configuration.

### 3. Replicated log

- [x] Implement term, index and event fields, replication, commit and recovery (step 2; host-tested).
- [ ] Record cockpit state, node up/down and each node's individual CAN A/B link up/down events (event codes done in step 1; detection from the heartbeat monitor is step 4).
- [ ] Verify consistent ordering and recovery after node and link failures (passes in the simulator; hardware still to do).

### 4. Raft state machine

- [x] Implement log-applied network graph mutations and `getNetwork()`
  (`common/raft/netstate.c`; host-tested, 2026-11-12).
- [ ] Verify each node converges on the same committed network state (host
  test: replaying one log on two nodes gives the same graph; on hardware
  still to do).

### 5. Routing and relaying

- [ ] Derive routing tables from each node's committed network state.
- [ ] Handle multi-link failure and information propagation, including relayed traffic and recovery.
- [ ] Confirm the handout's Part 5 checkpoint and replay behavior against the final design.

### 6. Time synchronization

- [x] Implement leader reference messages, follower global time and local drift correction (step 3; host-tested, bench app built; scope check still to do).
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

### Step 1 (branch `lab5-raft-core`, 2026-11-12)

Design: [design.md](design.md). Step 1 adds the portable pieces, with no
hardware glue yet:

- `common/raft/raft_config.h`: the shared config (all handout keys, plus the
  random-mode seed and the relay-capable nodes).
- `common/raft/raft_proto.{h,c}`: IDs and 8-byte layouts for REF, REQ_VOTE,
  VOTE, APPEND, APPEND_ACK, EVENT and EVENT_RESP.
- `common/raft/netstate.{h,c}`: log events, the four mutators,
  `getNetwork()`, and the routing table (which node relays which frames, and
  whether the graph is partitioned).
- `tests/`: 14 host tests, 533 checks, all passing. They cover every
  fault-matrix routing case (no relay, the four relay cases, the
  steering/drivetrain partition, an isolated node).

**Running the tests:**
- Any machine with gcc (the Pi, or WSL after `sudo apt install -y gcc make`):
  `make -C Lab5_Time_Sync/tests`
- Without a host compiler, on an emulated Cortex-M3 (Zephyr SDK QEMU), from
  the zephyrproject workspace:
  ```
  west build -p always -b qemu_cortex_m3 <repo>/Lab5_Time_Sync/tests/qemu -d build_lab5_tests
  west build -d build_lab5_tests -t run      # Ctrl+A, then X, to quit QEMU
  ```

### Step 2 (branch `lab5-raft-core`, 2026-11-12)

The Raft agent and client, still hardware-independent:

- `common/raft/raft_log.{h,c}`: a ring of the 256 most recent entries plus a
  snapshot of the network graph (log compaction).
- `common/raft/raft.{h,c}`: election with **pre-vote**, replication over
  broadcast CAN frames, commit, the Raft client (EVENT, redirect, retries),
  **check-quorum**, and snapshot catch-up. The predefined-leader startup
  rules and the runtime `LEADER=cockpit` / `LEADER=random` choice are in
  `raft_init_with_leader()`. See design.md section 4.
- `tests/test_raft.c`: a three-node simulator with 11 scenario tests,
  following the checkoff:
  - boot with the predefined leader (no REQ_VOTE; REF every 20 ms; empty
    APPEND every 100 ms) and with the Pi booting 5 s late;
  - client reports from all three nodes giving one entry at one index;
    redirect from a follower;
  - Pi down (one election, new leader within 1 s), Pi back (held entries
    replayed once, in order, marked replayed; leadership stays);
  - isolated follower (term unchanged thanks to pre-vote) and isolated
    leader (term changes exactly once; the old leader steps down);
  - `LEADER=random`, including finding a seed where the Pi wins;
  - snapshot catch-up of a rebooted node, and six runs at 15 % frame loss.

  Every scenario also checks Raft safety on each commit: no index is ever
  committed with two different entries, and no term has two leaders.

**Result:** 26 tests, 679 checks, all passing (QEMU Cortex-M3,
2026-11-12). The tests found one real bug, fixed in this step: catch-up
APPENDs for already-committed entries carried "no commit information", so a
rejoining node did not mark them replayed.

### Step 3 (branch `lab5-raft-core`, 2026-11-12)

Global time and PPS, plus a two-Nucleo bench app to check them on the
scope before the Raft glue goes in:

- `common/raft/gtime.{h,c}` (portable): the drift-corrected mapping from
  local to global time, REF samples, outlier rejection, steps when the
  master changes, and `global_gettime()`'s maths.
- `common/zephyr/` (Lab 5 copies of the Lab 3 files): `can_link` now
  accepts the Raft frames and timestamps the end of each frame from the
  MCP2515 INT line in an ISR. `gsync.{h,c}`:
  - per-bus REF send times;
  - pairing on the follower;
  - `global_gettime()` and `sys_clock_settime()`;
  - the PPS output with one console line per edge, and a 1 s `DRIFT` line;
  - the LEADER, SYNC and REF_RX pins.
- `sync_bench/`: both Nucleos run time sync with a stand-in for the
  election (N2 leads; N3 takes over after 100 ms without a REF). B1 toggles
  "silent" on a node.
- `tests/test_gtime.c`: 6 tests. Lock-in with ±40/−30 ppm crystals and
  0–4 µs stamp jitter (error ≤ 10 µs, drift estimate within 3 ppm); 1 s
  holdover (≤ 10 µs, against 70 µs without drift correction); outliers and
  steps; leader handover without a step; the 4000 s wrap.

**Result:** 32 tests, 740 checks, all passing (QEMU Cortex-M3). Both bench
images build without warnings (N2 and N3: 46.8 KB flash, 20.5 KB RAM).

**Bench (2026-11-13):** N2 master, N3 follower, Pi off.
- **Scope: PPS spread N2 vs N3 = 1.96 µs** (A2 on both boards,
  `GSYNC_NET_DELAY_US` = 0). That is about one bit time at 500 kbit/s,
  which matches the MCP2515 flagging a received frame about one bit before
  the sender flags its transmission complete. The follower (N3) led, by
  1.1–2.0 µs from edge to edge.
- **Calibrated: `GSYNC_NET_DELAY_US` = −2 µs (one bit). Spread after
  calibration: under 1 µs.** The remaining jitter comes from whole-µs
  timestamps on both nodes and interrupt entry time.
- **Handover and holdover (B1 on N2):** N2 went silent and N3 took over as
  master, with no visible change in either PPS on the scope. N3 then kept
  N2's time base using its drift estimate (−4.35 ppm). After about 83 s,
  N2 rejoined as a follower with no step and no outliers: its worst error
  in that second was 6 µs. Without drift correction the two boards would
  have been about 360 µs apart (4.35 ppm × 83 s).
- N3's error at each REF (`err@REF`): 0 ± 1 µs; worst over each second:
  1–2 µs.
- Drift of N3 against N2's global time: −5.3 to −5.5 ppm, stable.
- 0 untrusted stamps, 0 outliers, 0 steps. PPS edges fire 1–2 µs after
  their target (busy-wait exit).
- Global time carried over each time a board was reflashed: the other
  board took over as master and the rebooted one followed it, with no step.

The bench found two bugs, both fixed:
1. **PPS hang:** a node whose global time stepped at first sync (booting
   while the other board was already master) was left waiting for an edge
   about 60 s in the past. The negative local time, cast to an unsigned
   cycle count, made the busy-wait endless; the cooperative PPS thread then
   starved the console. The PPS thread now picks the edge again after any
   step and sleeps in 100 ms slices.
2. **Bus saw-tooth:** samples from both buses were mixed. The two INT edges
   of one REF are serviced one after the other, so bus A and bus B pairs
   differ by a few µs, and `err@REF` sat near −10 µs. Samples now come from
   one bus, switching only after 40 ms without a sample on it.

Flashing two boards from one PC: this board's OpenOCD config ignores
`west flash --serial`. Use
`west flash -d <build> -r openocd --cmd-pre-init "adapter serial <ST-Link SN>"`.

**Pins (both STM32s):** PPS **PA4 (A2)**, REF_RX **PB10 (D6)**, LEADER
**PC4** (CN10-34), SYNC **PC5** (CN10-6). All four were unused in Lab 3.

**Bench run:**
1. Build and flash, from the zephyrproject workspace:
   ```
   west build -p always -b nucleo_f401re 18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/sync_bench -d build_bench_n2 -- -DCONFIG_SYNC_BENCH_NODE=2
   west flash -d build_bench_n2
   west build -p always -b nucleo_f401re 18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/sync_bench -d build_bench_n3 -- -DCONFIG_SYNC_BENCH_NODE=3
   west flash -d build_bench_n3
   ```
   Same CAN wiring as Lab 3. The Pi can stay off.
2. Consoles: N2 prints `ROLE N2: master is N2`, N3 `master is N2`. Each
   prints one `PPS` and one `DRIFT` line a second. On N3, `drift` should
   settle within a few seconds (crystals: tens of ppm), `err@REF` should be a
   few µs, and `bad stamps` should stay near 0.
3. Scope: CH1 = N2 A2, CH2 = N3 A2, trigger on CH1's rising edge. Measure
   the edge spread over a minute. Any constant offset is
   `GSYNC_NET_DELAY_US`: set it and rebuild.
4. Handover: press B1 on N2. N3 becomes master after 100 ms, with no visible
   jump in N3's own PPS. Press B1 again: N2 follows N3, and its first
   `err@REF` shows how far it drifted while silent.

### Step 4 (branch `lab5-raft-core`, 2026-11-13)

Raft, time sync and routing on all three nodes.

- **STM32 apps** (`stm32_steering/`, `stm32_drivetrain/`): Lab 5 copies of
  the Lab 3 apps. `common/zephyr/raft_node.c` runs the Raft agent in its own
  thread, sends REF through `gsync`, and runs the client every 10 ms.
  LEADER=cockpit by default; holding B1 while a board resets switches that
  board to LEADER=random (`CONFIG_LAB5_LEADER_RANDOM` sets the default).
- **Pi cockpit** (`pi_cockpit/`): Lab 5 copy of the Lab 3 cockpit with the
  same Raft core, plus `pi_sync.c`.
  - Time sync uses SocketCAN kernel timestamps: received frames, and the
    echo of each sent frame when its transmission completes. They are
    converted to `CLOCK_MONOTONIC_RAW`, so NTP adjustments don't look like
    drift.
  - The heartbeat keeps running without the wheel proxy (the cockpit is in
    ERROR, not dead).
  - GPIOs: PPS GPIO17 (pin 11), REF_RX GPIO4 (pin 7), LEADER GPIO26
    (pin 37), SYNC GPIO16 (pin 36).
  - `lab5-cockpit.service` brings up CAN and starts the cockpit at boot.
    Flags go in `~/lab5-cockpit.env` (`COCKPIT_ARGS=--rt --leader=random`),
    output in `~/lab5-cockpit.log`. Setup: `pi_cockpit/install_service.sh`
    (once, with sudo).
  - `boot_can.sh` raises the CAN driver's threads to real-time priority
    (spi0 pump FIFO 61, MCP2515 interrupt threads FIFO 60). That steadied
    the kernel timestamps.
- **Client** (`netstate_observe`, shared): a node reports its own links (it
  hears someone on that bus or not), other nodes up/down, and the cockpit's
  status, but only while it hears at least one other node. A node that hears
  nobody reports only itself. Reports that stop being true are withdrawn.
- **Routing** (`relay.c`): forwards only what the routing table says. The
  table comes from the committed graph, plus the links each node's latest
  heartbeat reports (handout 5.2: the relay starts before the failure
  commits). REF is relayed only when the leader is off the target bus.
- **Shared REF pairing** (`refsync.c`, used by the STM32s and the Pi): first
  copy per bus, one sync bus at a time, relay delay tied to the relayed
  stamp.

**Tests:** 36 host tests, 796 checks, all passing (QEMU and gcc on the Pi).

**Hardware, 2026-11-13** (Pi + N2 + N3, no wheel proxy):
- *Cold start (checkoff 1-2):* the Nucleos boot in term 1 following the
  cockpit and wait; the Pi starts about 8 s later and leads term 1 with no
  election. One entry, `#1 COCKPIT_OK`, on all three nodes. candump on bus A
  over 2 s: REF 0x025 every 20 ms (100 frames), empty APPEND 0x041 every
  100 ms (20), APPEND_ACK 0x046/0x047, all three heartbeats, and no
  REQ_VOTE.
- *Pi down (checkoff 6):* `systemctl stop` on the Pi. One election: N2
  leads term 2 and commits `#2 NODE_cockpit_DOWN`, `#3 COCKPIT_ERROR`.
- *Pi back (checkoff 7):* it rejoins as a follower and LEADER stays on N2.
  It replays #1-#4 in order, each marked `(replayed)`, then the live entries
  `COCKPIT_OK` and its two links up. Global time carries across: the Pi
  measures +2 ppm against N2's continued time base.
- *Sync with the Pi as master:* the followers' worst error per second is
  22-30 µs, with no steps. The Pi's kernel timestamps limit it; N2 and N3
  track the same timestamps, so they stay together. The Pi's raw clock is
  about 95 ppm from the STM32s'.
- About 40 % of the STM32s' REF stamps are rejected as untrusted when the Pi
  is master (4 % with an STM32 master). About 45 samples a second remain.
  Not yet understood.

Bugs found on hardware and fixed:
- A board booting alone committed "my links are down".
- Raft debug lines overflowed the 1 KB log buffer at boot.
- A late relayed copy took the relay delay of the wrong stamp.
- Bursts of late Pi stamps caused steps (now 10 outliers in a row, not 3).
- The client re-sent the same report every 200 ms while no leader was
  reachable.

**Jumper runs, 2026-11-13** (Pi + N2 + N3; all three logs identical in
every run):
- *N3 B out/in (checkoff 4):* exactly one entry each way, term unchanged.
- *Pi off B + N3 off A (checkoff 5):* exactly the two expected entries
  (`CANLINK_drivetrain_A_DOWN`, `CANLINK_cockpit_B_DOWN`), term unchanged,
  after the node-down fix below.
- *N3 isolated while leader (checkoff 10):* N3 pre-voted for 9 s without
  raising its term. The others elected a new leader (one term change). N3
  rejoined and replayed what it missed.
- *Pi power-off (checkoff 6-7):* one election, `NODE_cockpit_DOWN` and
  `COCKPIT_ERROR`; the Pi booted, rejoined and the cockpit came back up.
- In one run, pulling N3's A disturbed the whole of bus A for about 30 s
  (every node lost bus A; the Pi's watchdog reset its interface six times).
  Pulling the same jumper again later behaved normally, so it was a contact
  glitch, not the software.

Bugs found by these runs and fixed (each has a simulator test):
- **Rebooted follower never caught up:** the leader kept the follower's old
  `match_index` (standard Raft assumes logs survive a reboot; ours are in
  RAM), and the stuck follower starved the other one, so nothing committed
  any more. Now a shorter log resets the match, and followers take turns.
- **Stale report committed after reconnecting:** queued reports that are no
  longer observed are dropped (`raft_client_retain`).
- **Cockpit committed down during checkoff 5:** a node reported another node
  down after 60 ms, before the relay had started. Node and cockpit down now
  need `RAFT_NODE_DOWN_MS` = 200 ms of silence; links stay at 60 ms.
- **Corrupted terms:** while bus A was disturbed, the Pi received frames
  decoding as term 0, 248 and 252 (frames with a Raft ID and another frame's
  data are suspected). The cluster adopted them, and once the term reached
  255 the next election asked for term 0 and no leader could ever be
  elected. Votes and acknowledgements more than 32 terms ahead are now
  dropped and logged with their raw bytes; the term stops at 255 instead of
  wrapping. Source still to be found from those logs.

**Still to test (needs jumpers):** checkoff 9 (relayed sync with an STM32
master), and a clean rerun of 4 and 5 with these fixes.

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
    on the scope. (Step 3: both ends stamp the MCP2515 interrupt edge at the
    end of the frame, so the delay is close to 0.)
12. **Relayed sync delay.** Step 9 syncs a cut-off STM32 through a relayed
    `REF`. Should the follower correct for the extra relay hop?
    *Assumption:* yes. Relays keep forwarding frames unchanged (a "relayed"
    flag caused same-ID collisions in Lab 3), so the follower uses its routing
    table: if the leader has no link on the bus the `REF` arrived on, the
    `REF` was relayed and the follower adds the measured relay delay.
