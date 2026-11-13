# Lab 5 design

Status: **implemented and tested on hardware** (Pi + two Nucleos,
2026-11-13). This file describes the system as built. Measurements, bench
runs and the bugs they exposed are in
[Progress_doc_lab5.md](Progress_doc_lab5.md), together with the questions
for the course staff; the report is `Doc/lab5/lab5_report.md`. Anything the
TA answers differently changes the matching section.

Nodes keep their Lab 3 numbering: **N1 = cockpit (Pi)**, **N2 = steering**,
**N3 = drivetrain**. Buses: **A = CAN 1**, **B = CAN 2** in the handout's
figure.

## 1. Code layout

Lab 5 has its own copy of the Lab 3 apps, so the Lab 3 code stays as graded:

```
Lab5_Time_Sync/
  common/            can_proto (Lab 3 catalog + BLINK start time)
    raft/            portable C, no Zephyr or Linux calls:
      raft_config.h    the config keys (one file, identical on all nodes)
      raft_proto.c/.h  Lab 5 frame IDs, layouts, CRC-4
      raft.c/.h        roles, terms, votes, replication, commit, client
      raft_log.c/.h    ring buffer + snapshot (log compaction)
      netstate.c/.h    state machine, getNetwork(), routing, client detection
      gtime.c/.h       global time: offset, drift ratio, global_gettime()
      refsync.c/.h     REF send/receive pairing (STM32s and Pi alike)
    zephyr/          STM32 glue: can_link, zone_core, relay (Lab 3 copies,
                     extended), gsync (time sync, PPS), raft_node (Raft
                     thread, client, routing)
  stm32_steering/    N2 app
  stm32_drivetrain/  N3 app
  pi_cockpit/        N1 cockpit: Lab 3 cockpit + the same Raft core,
                     pi_sync (time sync on Linux), systemd service
  sync_bench/        step 3 bench: time sync + PPS on the two Nucleos
  tests/             host tests: three simulated nodes, scripted faults
  tools/             report PDF export
```

The Raft core is plain C, like `can_proto.c` in Lab 3. It is compiled into
both STM32 apps and the Pi, so all three run the same consensus code. The
core never touches hardware: each platform calls `raft_tick(now)` and
`raft_on_frame(...)` and supplies a `send()` callback. The same code runs in
`tests/` (38 tests, QEMU Cortex-M3 or gcc on the Pi), against a simulated bus
with link and node failures.

## 2. Configuration (`raft_config.h`)

| Key | Value | Notes |
|---|---|---|
| `N` | 3 | majority = 2 |
| `default_leader` | `cockpit` or `none` | chosen at boot: `LEADER=cockpit` / `LEADER=random` (section 4) |
| `heartbeat_ms` | 20 | `REF` period; also the sync period |
| `missed_heartbeats` | 3 | |
| `election_timeout_jitter_ms` | 39 | timeout = 3 × 20 + U(0, 39) ms |
| `log_size` | 30000 | logical size; see section 5 |
| `nodes` | cockpit = 1, steering = 2, drivetrain = 3 | |
| `debug` | yes | prints election and append messages |
| `seed` | 0x740 (steering wins); 0x209 = `RAFT_SEED_PI_WINS` | random-mode jitter seed |
| `RAFT_NODE_DOWN_MS` | 200 | a node unheard this long is reported down (links: 60 ms) |
| `RAFT_CLUSTER_WAIT_MS` | 60000 | random mode: fallback if a node never appears |

## 3. CAN message catalog additions

Every Lab 5 frame has a **separate ID per sender** (base + node number, like
the Lab 3 heartbeats). Two nodes that briefly both think they are leader then
send different IDs, never the same ID with different data. Same-ID
collisions with different data are the bit-error problem we hit in Lab 3.

All frames are DLC 8 on both buses. In Raft frames, byte 0 holds the 4-bit
seq in its low nibble and a **CRC-4 (x⁴+x+1) of bytes 1–7** in its high
nibble. CAN's own CRC protects the wire, but a loose SPI wire on the Pi
delivered frames with a Raft ID and garbled data. Terms are 8 bits and log
indexes 16 bits.

| ID (N1/N2/N3) | Name | From → to | Payload (bytes 1–7) | Rate |
|---|---|---|---|---|
| 0x010 | CTRL | N1 → N2, N3 | Lab 3, unchanged | 20 ms |
| 0x020 | FAULT | any → all | Lab 3, unchanged | event |
| 0x025/6/7 | **REF** | leader → all | b1 term · b2 cycle counter · b3–b6 master time: global µs mod 4000 s when the *previous* REF finished *on this bus* · b7 flags (time valid, failure mode) | 20 ms |
| 0x031/2/3 | **REQ_VOTE** | candidate → all | b1 term · b2–b3 last log index · b4 last log term · b5 pre-vote | election |
| 0x035/6/7 | **VOTE** | voter → candidate | b1 term · b2 candidate · b3 granted · b4 pre-vote | election |
| 0x041/2/3 | **APPEND** | leader → all | b1 term · b2–b3 index · b4 entry term · b5 previous entry's term · b6 event (0 = empty) · b7 commit delta (commit = index − delta; 255 = unknown) | per entry; empty every 100 ms and when the commit index moves |
| 0x045/6/7 | **APPEND_ACK** | follower → leader | b1 term · b2 success · b3–b4 match index (on failure: the follower's last index) | per APPEND |
| 0x051/2/3 | **SNAPSHOT** | leader → all | b1 term · b2–b3 index · b4 its term · b5–b6 network state (16 bits) | when a follower needs compacted entries |
| 0x061/2/3 | **EVENT** | client → leader | b1 addressed leader · b2 event · b3 client seq | until answered, every 50 ms |
| 0x065/6/7 | **EVENT_RESP** | leader → client | b1 client · b2 client seq · b3 result (OK / REDIRECT / NO_CHANGE / NO_LEADER) · b4 leader · b5–b6 index | per EVENT |
| 0x100–0x102 | HB_Nx | each node | Lab 3, unchanged; b2 = the sender's own link health, used for routing | 20 ms |
| 0x200 | BLINK | N1 → N2, N3 | Lab 3 + b3–b6 start time (global µs mod 4000 s), b7 = 1 | state change, then resent at each Lab 3 toggle |
| 0x300 | FORCE | N2 → N1 | Lab 3, unchanged | 50 ms |

Because REF carries a different time on each bus, its two copies differ. A
REF is therefore relayed only when the leader is off the target bus
(section 6).

**Priorities follow Lab 1 R4.4:** the safety messages (CTRL with the brake,
FAULT) keep the lowest IDs. `REF` comes right after them, ahead of the Raft,
heartbeat and convenience frames. Its own queuing delay does not affect the
sync, because each REF carries the measured send time of the previous REF
rather than the time it was queued.

**Event codes (1 byte):** `COCKPIT_OK`, `COCKPIT_ERROR`, `NODE_X_UP/DOWN`
(X = 1..3), `CANLINK_X_Y_UP/DOWN` (X = 1..3, Y = A/B): 20 codes.

**Added bus load:** REF 1.35 % + empty APPEND 0.27 %, plus a few frames per
change. A relay forwards only the frames the routing table names, which at
most doubles that. The total stays far below the 50 % limit.

## 4. Raft agent

- **Startup with `default_leader = cockpit`:** every node boots in term 1
  with N1 as leader, so no election is held. Two rules make that work with a
  Pi that boots about 30 s after the Nucleos, or reboots mid-run:
  1. A follower starts its election timer only after it has heard the
     current leader once. The Nucleos therefore wait for the Pi at cold start
     instead of electing among themselves (checkoff step 1).
  2. A node that boots as the default leader first listens for one election
     timeout. If it hears a REF with a higher term, it joins as a follower.
     That is how the Pi rejoins after step 6 without taking leadership back
     (step 7).
- **Startup with `default_leader = none` (LEADER=random):** every node waits
  until it hears all three nodes' heartbeats (`raft_wait_for_cluster()`;
  fallback: a majority for 60 s), then starts its timer with a first timeout
  of base + `raft_boot_jitter_ms(seed, node)`, a fixed hash. The seed, not
  the boot order, decides the first leader. Seed 0x740: steering 1 ms, the
  others 39 ms. Seed 0x209: cockpit 2 ms, the others 38 ms. The margins
  cover the Pi starting its timer up to ~20 ms later than the Nucleos.
- **Choosing the mode at the bench:** STM32: holding B1 at reset gives
  random mode (default seed), held 3 s more gives the Pi-wins seed
  (`CONFIG_LAB5_LEADER_RANDOM` flips the default). Pi:
  `COCKPIT_ARGS=--rt --leader=random [--seed=pi]` in `~/lab5-cockpit.env`.
  All three nodes must use the same mode and seed.
- **Election:** standard Raft (RequestVote with log up-to-date check,
  majority of 2), plus two standard extensions that checkoff step 10 needs:
  - **Pre-vote:** a node that times out first asks whether a majority would
    vote for it, and raises its term only if so. A node only grants a
    pre-vote if it has not heard a leader for 60 ms. An isolated node never
    gets that majority, so its term never moves, and reconnecting it causes no
    election: the term changes only when the leader itself is lost.
  - **Check-quorum:** a leader that hears no APPEND_ACK from a majority for
    300 ms steps down without changing the term, so an isolated leader turns
    its LEADER LED off.

  A link failure never triggers an election, because the leader's REF still
  reaches every node over the other bus or the relay.
- **Terms:** compared as plain numbers, and the term stops at 255 rather than
  wrapping (wrap-around comparison would make a rebooted node's term 1 look
  newer than a high cluster term). Votes and acknowledgements more than 32
  terms ahead are dropped and logged; leader messages are kept, because a
  rebooted node learns the term from them. Before the CRC and this guard,
  garbled frames drove the cluster to term 255, where the next election
  asked for term 0 and deadlocked.
- **Replication:** the leader appends one entry per APPEND, waits for
  APPEND_ACKs, and commits at a majority. Followers apply committed entries
  in index order. A rejoining node is caught up by Raft back-off, which
  replays the entries it missed; they print as `replayed` (question 6).
  Two changes, because our logs live in RAM (standard Raft assumes they
  survive a reboot):
  - a follower whose failure-ack shows a log shorter than its recorded match
    has rebooted, and the leader resets the match. Before this, the leader
    waited forever for a log that no longer existed;
  - lagging followers are served in turn, so one stuck follower cannot
    starve the other.
- **Client requests:** a client sends EVENT to the node it believes is
  leader. A follower answers EVENT_RESP = redirect with the leader's ID, and
  the client resends. The leader appends an event only if it changes the
  projected state (committed plus pending), so three clients reporting the
  same failure produce one entry.
- **Persistence:** all three nodes keep the log in RAM. A rebooted node
  (Pi included) rejoins with an empty log and is caught up by replay, or by
  one SNAPSHOT frame if the entries it needs were compacted.

## 5. Log and state machine

- **Log:** a ring of the 256 most recent entries (term, event, flags:
  3 bytes each; the index is implicit) plus a **snapshot** of the network
  graph as of the last compacted index. Indexes run to `log_size`. The
  state machine is six link bits, three node bits and the cockpit bit, so the
  snapshot fits in one SNAPSHOT frame (question 8).
- **State machine (`netstate.c`):** `addLink(X,Y)`, `dropLink(X,Y)`,
  `addNode(X)` (back with no links until its `addLink`s), `dropNode(X)`
  (removes its links), applied when an entry commits. The initial state has
  everything up and the cockpit in ERROR (the system powers up in error).
  `getNetwork()` returns a copy of the graph.

## 6. Raft client and routing

- **Detection** (`netstate_observe()`, every 10 ms, from the per-bus
  heartbeat times that Lab 3's HB_MON already keeps). A node vouches only for
  what it can tell apart:
  - **its own links:** link Y is up if it hears *anyone* on bus Y within
    60 ms. A pulled jumper cuts both directions, so a node's own view of its
    links is reliable (unlike "I can't hear X on A", which X's or my link
    could cause);
  - **other nodes:** node X is down after 200 ms unheard on both buses; for
    the cockpit that also means `COCKPIT_ERROR`;
  - **nothing about others while it hears nobody**, and nothing at all in
    the first 200 ms after boot. An isolated node cannot tell whether it
    failed or they did. (Seen on hardware: a board booting alone committed
    "my links are down" the moment it won an election.)
- **Reporting:** a report differing from the committed graph is sent (and
  re-sent every 200 ms while still not committed and not queued). Queued
  reports no longer observed are dropped (`raft_client_retain()`), so a node
  that was cut off doesn't commit stale reports when it reconnects.
- **Routing table:** each node builds it from `getNetwork()`. Relay R
  forwards node X's frames from bus Y to the other bus when:
  1. X has no link on that other bus;
  2. some live node there has no link on Y;
  3. R is the lowest-numbered relay-capable node with links on both buses.

  Exactly one node relays, and every node agrees which one. The Pi is not
  relay-capable: Linux has no worst-case timing bound (Lab 3, 4.2). This
  replaces Lab 3's always-on relay.
- **The 5.2 edge case (two links failing):** before the second failure is
  committed, the graph is out of date, and the report may itself need the
  relay. Two rules close the gap:
  1. **Heartbeats carry link health.** Each HB carries its sender's own view
     of its links (b2). Each node overlays these on the committed graph
     (`netstate_local_view()`), and RELAY forwards if either the committed
     or the overlaid table says so. The relay starts about one heartbeat
     after the cut link is detected, before anything commits.
  2. **Node-down needs 200 ms, links only 60 ms.** With one 60 ms timeout, a
     node that could hear another only through the not-yet-started relay
     reported it down, and the cockpit was committed down while it was fine.
- **REF relaying:** only when the overlaid table says the leader is off the
  target bus. Then the leader isn't sending there, so the relayed copy (with
  the other bus's time) cannot collide with the leader's own copy.
- **Not covered:** N2 and N3 on opposite buses; only the Pi could bridge
  them (question 13).

## 7. Time synchronization

- **Master:** the Raft leader. It sends REF every 20 ms. Each REF carries,
  per bus, the master time at which the *previous* REF finished on that bus.
  Both ends stamp the end of the frame on the wire:
  - **STM32s:** `can_link` stamps the falling edge of each MCP2515's INT line
    in an ISR (beside the driver's callback). INT falls on transmit-complete
    (TXnIF) and on receive (RXnIF). A stamp older than 200 µs when the driver
    hands over the frame may belong to an earlier frame and is not used.
  - **Pi:** kernel `SO_TIMESTAMPNS` on received frames and on the echo of
    each sent frame (`CAN_RAW_RECV_OWN_MSGS`; mcp251x has IFF_ECHO, so the
    echo comes at transmit-complete). Stamps are converted to
    `CLOCK_MONOTONIC_RAW` so NTP doesn't look like drift. The CAN driver's
    threads stay at their default priorities: raising them coincided with
    driver errors.
- **Wire format:** master time is sent modulo 4000 s (4·10⁹ µs, below 2³²).
  A plain 32-bit µs counter wraps after 4294.97 s, which is not a whole
  number of seconds, so two nodes one wrap apart would place their PPS edges
  0.97 s apart. With a 4000 s modulus any ambiguity is whole seconds.
- **Followers** (`refsync.c`, shared by `gsync.c` and `pi_sync.c`) pair the
  master time from REF k with their receive stamp of REF k−1 *on the same
  bus*:
  - only the first copy of a REF per bus counts (a relayed copy is later);
  - samples come from one bus at a time, switching after 40 ms without one.
    The two buses' stamps of one REF differ by a few µs, and mixing them
    gave a −10 µs saw-tooth;
  - offset: `global = L(k−1) + network_delay`. The delay is −2 µs, measured:
    the receiver flags a frame one bit before the sender flags
    transmit-complete. A stamp that came through a relay gets +400 µs
    (mean error ≈ 0 on the bench);
  - drift: `ratio = (L(k) − L(j)) / (F(k) − F(j))` over pairs at least 4 s
    apart, low-pass filtered (weight 1/4). Over 1 s, the Pi's ±40 µs stamps
    read as ±40–80 ppm;
  - outliers: a sample more than 150 µs off is ignored; 10 in a row means the
    master time really moved (a new master), and the clock steps.
- **`global_gettime()`** returns
  `global_at_sync + (local_now − local_at_sync) × ratio`. On the Nucleos each
  sample also sets the Zephyr clock (`sys_clock_settime()`), as the handout
  asks. The Pi keeps its own offset instead, because resetting the Linux
  system clock would fight NTP.
- **Leader handover (step 6):** the new leader freezes its mapping and
  continues its own `global_gettime()`; followers see no step beyond the
  drift accumulated during the election. Measured: FL/RL 0.74 µs apart at
  4 Hz after the Pi failed, with no visible step.
- **Drift print:** a 1 s task prints the ratio and the correction currently
  applied (`DRIFT` lines).
- **PPS:** a GPIO on every node rises at each whole second of
  `global_gettime()`, with a console line per edge (global time, local time,
  drift ratio, correction since the last REF). STM32: a cooperative thread
  sleeps until 300 µs before the edge, then busy-waits. Pi: a SCHED_FIFO
  thread sleeps until 2 ms before, then busy-waits.
- **LEDs and test points:**
  - `LEADER` lit on the leader;
  - `SYNC` lit while global time is valid (master, or a sample in the last
    60 ms);
  - `REF_RX` toggled on each new REF.
  - Pins, STM32s: PPS PA4 (A2), REF_RX PB10 (D6), LEADER PC4, SYNC PC5. Pi:
    PPS GPIO17 (pin 11), REF_RX GPIO4 (pin 7), LEADER GPIO26 (pin 37), SYNC
    GPIO16 (pin 36), HZ GPIO12 (pin 32).

## 8. Blinkers on global time

Lamp phase comes from global time, so FL (N2) and RL (N3) switch at the same
global instants with no frame per edge (`zone_core.c`; the Pi's HZ output
follows the same rule):

- **Turn signals:** BLINK carries a start time, set by N1 50 ms in the
  future. That keeps the Lab 3 start deadline of 100 ms without waiting for a
  500 ms boundary. Lamps are on while `((global − start) / 500 ms)` is even.
- **Hazards:** failure mode (`COCKPIT_ERROR` committed) flashes at 4 Hz;
  other error states flash at 2 Hz (question 4). The phase is anchored to
  whole multiples of the half-period of global time, so a node entering
  failure mode late still flashes in step.
- **Edges:** `gsync_wait_until()` sleeps until 300 µs before the edge, then
  busy-waits with the scheduler locked, so edges land within a few µs.
  Measured: FL vs RL 0.5 µs (hazards), 0 s and 1 µs (turn signals); FL vs
  HZ 46 µs.
- **Lamp pins:** FL N2 PB9 (D14), FR N2 PB8 (D15), RL N3 PB9 (D14),
  RR N3 PB8 (D15).

## 9. Status

| Step | Status |
|---|---|
| 1. Config, catalog, netstate + host tests | done |
| 2. Raft agent and log, three-node simulator | done |
| 3. gtime + PPS on the Nucleo pair | done; PPS spread < 1 µs |
| 4. Glue into N2, N3 and the Pi; client; routing | done; checkoff 1–2 and 4–8, 10 on hardware |
| 5. Blinkers on global time; LEADER/SYNC/REF_RX | done; checkoff 1, 3, 6 on the scope |
| 6. Checkoff run, scope evidence, report | report written; checkoff 9 open (question 13) |

The host tests run on QEMU (Zephyr SDK) or with gcc on the Pi
(`make -C Lab5_Time_Sync/tests`).
