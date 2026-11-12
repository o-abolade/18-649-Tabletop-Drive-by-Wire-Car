# Lab 5 design (draft for team review)

Status: **proposal, not implemented.** It builds on the working Lab 3 system
(`lab3_CAN/`) and on the working assumptions in
[Progress_doc_lab5.md](Progress_doc_lab5.md) ("Questions for the course
staff"). Anything the TA answers differently changes the matching section.

Nodes keep their Lab 3 numbering: **N1 = cockpit (Pi)**, **N2 = steering**,
**N3 = drivetrain**. Buses: **A = CAN 1**, **B = CAN 2** in the handout's
figure.

## 1. Code layout

Lab 5 gets its own copy of the Lab 3 apps, so the Lab 3 code stays as graded:

```
Lab5_Time_Sync/
  common/            can_proto (Lab 3 catalog + the Lab 5 frames below)
    raft/            portable C, no Zephyr or Linux calls:
      raft_config.h    the config keys (one file, identical on all nodes)
      raft.c/.h        roles, terms, votes, replication, commit
      raft_log.c/.h    ring buffer + snapshot (log compaction)
      netstate.c/.h    the state machine: graph, getNetwork(), routing table
      gtime.c/.h       global time: offset, drift ratio, global_gettime()
    zephyr/          Lab 3 can_link / zone_core / relay + Raft and PPS glue
  stm32_steering/    N2 app (Lab 3 + Lab 5 glue)
  stm32_drivetrain/  N3 app
  pi_cockpit/        N1 cockpit (Lab 3 + the same Raft core)
  tests/             host tests: three simulated nodes, scripted faults
```

The Raft core is plain C, like `can_proto.c` in Lab 3: it is compiled into
both STM32 apps and the Pi, so all three run the same consensus code. The
core never touches hardware. Each platform calls `raft_tick(now)` and
`raft_on_frame(...)`, and gives it a `send()` callback and a microsecond
clock. That also lets `tests/` run three nodes against a simulated bus with
link and node failures before we flash anything.

## 2. Configuration (`raft_config.h`)

| Key | Value | Notes |
|---|---|---|
| `N` | 3 | majority = 2 |
| `default_leader` | `cockpit` or `none` | build option: `LEADER=cockpit` / `LEADER=random` |
| `heartbeat_ms` | 20 | `REF` period; also the sync period |
| `missed_heartbeats` | 3 | |
| `election_timeout_jitter_ms` | 39 | timeout = 3 × 20 + U(0, 39) ms |
| `log_size` | 30000 | logical size; see section 5 |
| `nodes` | cockpit = 1, steering = 2, drivetrain = 3 | |
| `debug` | yes | prints election and append messages |
| `seed` | per build | random-mode jitter seed (question 7) |

## 3. CAN message catalog additions

Every Lab 5 frame has a **separate ID per sender** (base + node number, like
the Lab 3 heartbeats). Two nodes that briefly both think they are leader then
send different IDs, never the same ID with different data. Same-ID
collisions with different data are the bit-error problem we hit in Lab 3.

All frames are DLC 8 on both buses, with byte 0 = 4-bit seq + flags as in
Lab 3. Terms are 8 bits (255 elections before wrap, which is far more than a
demo needs); log indexes are 16 bits.

| ID (N1/N2/N3) | Name | From → to | Payload (bytes 1–7) | Rate |
|---|---|---|---|---|
| 0x010 | CTRL | N1 → N2, N3 | Lab 3, unchanged | 20 ms |
| 0x020 | FAULT | any → all | Lab 3, unchanged | event |
| 0x025/6/7 | **REF** | leader → all | b1 term · b2 cycle counter · b3–b6 master time (µs, the send time of the *previous* REF) · b7 flags (failure mode) | 20 ms |
| 0x031/2/3 | **REQ_VOTE** | candidate → all | b1 term · b2–b3 last log index · b4 last log term | election |
| 0x035/6/7 | **VOTE** | voter → candidate | b1 term · b2 candidate · b3 granted | election |
| 0x041/2/3 | **APPEND** | leader → all | b1 term · b2–b3 index · b4 prev-entry term · b5 event (0 = empty) · b6–b7 commit index | per entry; empty every 100 ms |
| 0x045/6/7 | **APPEND_ACK** | follower → leader | b1 term · b2 success · b3–b4 match index | per APPEND |
| 0x061/2/3 | **EVENT** | client → leader | b1 addressed leader · b2 event · b3 client seq | on a detected change |
| 0x065/6/7 | **EVENT_RESP** | leader → client | b1 client · b2 client seq · b3 result (ok / redirect) · b4 leader id | per EVENT |
| 0x100–0x102 | HB_Nx | each node | Lab 3, unchanged | 20 ms |
| 0x200 | BLINK | N1 → N2, N3 | Lab 3 + b3–b6 start time (global µs) | event |
| 0x300 | FORCE | N2 → N1 | Lab 3, unchanged | 50 ms |

**Priorities follow Lab 1 R4.4:** the safety messages (CTRL with the brake,
FAULT) keep the lowest IDs. `REF` comes right after them, ahead of the Raft,
heartbeat and convenience frames. Its own queuing delay does not affect the
sync, because each REF carries the measured send time of the previous REF
rather than the time it was queued.

**Event codes (1 byte):** `COCKPIT_OK`, `COCKPIT_ERROR`, `NODE_X_UP/DOWN`
(X = 1..3), `CANLINK_X_Y_UP/DOWN` (X = 1..3, Y = A/B): 20 codes.

**Added bus load:** REF 1.35 % + empty APPEND 0.27 %, plus a few frames per
change. Raft frames are relayed (section 6), which at most doubles that. The
total stays far below the 50 % limit.

## 4. Raft agent

- **Startup with `default_leader = cockpit`:** every node boots in term 1
  with N1 as leader, so no election is held. Two rules make that work with a
  Pi that boots ~20 s after the Nucleos, or reboots mid-run:
  1. A follower starts its election timer only after it has heard the
     current leader once. The Nucleos therefore wait for the Pi at cold start
     instead of electing among themselves (checkoff step 1).
  2. A node that boots as the default leader first listens for one election
     timeout. If it hears a REF with a higher term, it joins as a follower.
     That is how the Pi rejoins after step 6 without taking leadership back
     (step 7).
- **Startup with `default_leader = none`:** every node starts as a follower
  and times out with the seeded jitter, giving an ordinary election.
- **Election:** standard Raft (RequestVote with log up-to-date check,
  majority of 2). A link failure never triggers one, because the leader's REF
  still reaches every node over the other bus or the relay.
- **Replication:** the leader appends one entry per APPEND, waits for
  APPEND_ACKs, and commits at a majority. Followers apply committed entries
  to the state machine in index order. A rejoining node is caught up by
  normal Raft back-off (the next-index decrements until the logs match),
  which replays the entries it missed. Those entries print as `replayed` on
  the Pi (question 6).
- **Client requests:** a client sends EVENT to the node it believes is
  leader. A follower that receives it answers EVENT_RESP = redirect with the
  leader's ID, and the client resends. The leader appends the event only if it
  changes the current state, so three clients reporting the same failure
  produce one entry.
- **Persistence:** the Pi keeps its log in a file. The Nucleos keep theirs in
  RAM; after a reset a Nucleo rejoins with an empty log and is caught up from
  the leader's snapshot.

## 5. Log and state machine

- **Log:** a ring buffer of recent entries (term, index, event: 4 bytes
  each) plus a **snapshot** of the network graph at the oldest kept index.
  Indexes run to `log_size`. The state machine is six link bits and three
  node bits, so the snapshot is tiny; a ring of a few hundred entries is
  plenty for the demo (question 8).
- **State machine (`netstate.c`):** `addLink(X,Y)`, `dropLink(X,Y)`,
  `addNode(X)`, `dropNode(X)` exactly as in the handout table, applied when an
  entry commits. `getNetwork()` returns a copy of the graph: node up/down, and
  for each node, link A and link B up/down, plus cockpit OK/ERROR.

## 6. Raft client and routing

- **Detection** reuses Lab 3's HB_MON, which already tracks every peer's
  heartbeat per bus:
  - `CANLINK_X_Y_DOWN`: node X has been unheard on bus Y for 60 ms while it
    is still heard on the other bus. That is the same test that drives the
    DEG LEDs.
  - `NODE_X_DOWN`: node X is unheard on both buses.
  - `COCKPIT_ERROR / OK`: N1's heartbeats stop or resume.
  - The matching `UP` events follow when the heartbeats return.
- **Routing table:** each node builds it from `getNetwork()`. Relay node R
  forwards node X's frames from bus Y to the other bus when all of these
  hold:
  1. X has no link on that other bus.
  2. Some node there needs X's frames.
  3. R is the lowest-numbered node with links on both buses.

  Exactly one node relays, and every node agrees which one, because they all
  hold the same committed graph. This replaces Lab 3's always-on relay.
- **The 5.2 edge case (two links failing):** before the second failure is
  committed, the graph is out of date, so relays chosen from it can be wrong.
  Two rules close the gap:
  1. **Raft frames are always relayed** (REF, APPEND, APPEND_ACK, REQ_VOTE,
     VOTE, EVENT, EVENT_RESP), using Lab 3's first-copy dedup. A report from a
     node cut off from the leader still reaches it, so the failure gets
     committed.
  2. **Locally detected, not-yet-committed failures** also enable the relay
     for application traffic (CTRL, BLINK, FAULT, HB) until the commit lands.
     Without this, CTRL to N3 would stop for one or more Raft rounds, the
     60–70 ms gap that made us relay always in Lab 3.

## 7. Time synchronization

- **Master:** the Raft leader. It sends REF every 20 ms. Each REF carries the
  master time at which the *previous* REF was handed to the controller,
  captured in the CAN TX path. That works around the MCP2515 having no
  hardware timestamp.
- **Followers** timestamp each REF on receipt in the RX callback. They pair
  the master time from REF k with their receive time of REF k−1, which is the
  same frame:
  - offset: `global = L(k−1) + network_delay` at local time F(k−1). The delay
    is the frame time plus the measured driver latency, calibrated with the
    PPS scope traces (question 11).
  - drift: `ratio = (L(k−1) − L(k−2)) / (F(k−1) − F(k−2))`, low-pass filtered
    over several REFs so one late interrupt doesn't swing it.
- **`global_gettime()`** returns `global_at_sync + (local_now − local_at_sync) × ratio`.
  On the Nucleos the sync also sets the Zephyr clock with `clock_settime()`,
  as the handout asks. The Pi keeps its own offset instead, because resetting
  the Linux system clock would fight NTP.
- **Relayed REF (checkoff step 9):** if the routing table says the leader has
  no link on the bus a REF arrived on, the REF was relayed. The follower adds
  the measured relay delay (question 12).
- **Leader handover (step 6):** the new leader was a synced follower, so it
  continues from its own `global_gettime()`. Followers see no step beyond the
  drift accumulated during the election, a few µs at crystal drift rates.
  That is well inside the 100 µs limit.
- **Drift print:** a 1 s task prints the ratio and the correction currently
  applied.
- **PPS:** a GPIO on every node rises at each whole second of
  `global_gettime()`, armed by a timer and finished with a short busy-wait.
  The line printed on each edge holds global time, local time, drift ratio,
  and the correction since the last REF.
- **LEDs and test points:**
  - `LEADER` lit on the leader.
  - `SYNC` lit while the follower's global time is valid (a REF in the last
    3 periods).
  - `REF_RX` pulsed on each REF received.

## 8. Blinkers on global time

Lamp phase comes from global time, so FL and RL switch on the same global
instant on both STM32s:

- **Turn signals:** BLINK carries a start time, set by N1 about 50 ms in the
  future. That keeps the Lab 3 start deadline of 100 ms without waiting for a
  500 ms boundary. Lamps are on while `((global − start) / 500 ms)` is even.
- **Hazards:** failure mode (`COCKPIT_ERROR` committed) flashes at 4 Hz;
  other error states flash at 2 Hz (question 4). The phase is anchored to
  whole multiples of the half-period of global time, so a node entering
  failure mode late still flashes in step.

## 9. Order of work

1. `raft_config.h`, catalog encoders and decoders, `netstate.c` with host
   tests.
2. `raft.c` and `raft_log.c` with the three-node host simulation: election,
   replication, link failure without election, leader failure, rejoin and
   replay.
3. `gtime.c` plus PPS on one Nucleo pair, scope check of PPS spread.
4. Glue into the STM32 zones and the Pi cockpit; Raft client from HB_MON;
   routing replaces always-relay.
5. Blinkers on global time; LEADER/SYNC LEDs; REF_RX test point.
6. Run the checkoff sequence; capture scope evidence; write the three report
   pages.

The host tests need a C compiler that builds for the PC. The Pi has gcc, so
the tests can run there if nothing is installed on the laptops.
