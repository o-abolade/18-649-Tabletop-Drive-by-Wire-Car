# Lab 5 checkoff: questions to be ready for

The handout says questions go to everyone on the team, and names "what would
change with four nodes". Each answer below is the short version to say out
loud; the detail is in `design.md` and the numbers are in
`Progress_doc_lab5.md`. N1 = cockpit (Pi), N2 = steering, N3 = drivetrain.

## Leader election

**Why is there no election at power-up?**
Predefined leader: every node boots in term 1 with the cockpit as leader. The
Nucleos don't start their election timers until they've heard the Pi once,
so they wait for it instead of electing each other. The Pi listens for one
election timeout before leading, so after a reboot it joins a newer leader
instead of taking over.

**How does a follower decide the leader is dead?**
It gets no REF (the heartbeat, every 20 ms) for 3 × 20 ms + a random
0–39 ms, i.e. 60–99 ms.

**Why doesn't pulling one link cause an election?**
The leader's REF still arrives on the other bus, or through the relay. Even
if a node misses REFs briefly, pre-vote stops it from raising the term.

**What is pre-vote, and why do we need it?** (checkoff 10)
Before raising its term, a node asks "would you vote for me?". Others say
yes only if they haven't heard a leader for 60 ms. An isolated node never
gets a majority, so its term never moves, and when it's plugged back in
nothing gets disrupted. Without pre-vote it would keep raising its term and
force an election when it reconnects.

**What is check-quorum?**
A leader that hears no acknowledgements from a majority for 300 ms steps
down (same term). An isolated leader therefore turns its LEADER LED off
instead of believing it still leads.

**How fast is a new leader elected when the Pi dies?** (checkoff 6)
- 60–99 ms to time out, plus a few ms for pre-vote and vote: about 100 ms.
- `COCKPIT_ERROR` (4 Hz hazards) commits about 150 ms after that, because a
  node must be silent for 200 ms before it's reported down.
- Measured: well under 1 s.

**Why can't there be two leaders?**
A leader needs votes from a majority (2 of 3) in its term, and each node
votes once per term. Two majorities of 3 always share a node. We also give
every Raft frame a separate CAN ID per sender, so two nodes that briefly both
think they lead never send the same ID with different data.

**What happens when the Pi comes back?** (checkoff 7)
It listens first, hears the STM32 leader's REF with a higher term, and
becomes a follower. LEADER stays on the STM32. Its log is empty (logs are in
RAM), so the leader resends every entry. Entries that arrive already
committed are printed `(replayed)`, once each, in order, then live entries.

**LEADER=random: how does the seed pick the winner?** (checkoff 8)
In random mode every node waits until it hears all three, then starts its
timer with base + a jitter computed from (seed, node). The shortest timeout
wins.
- Default seed 0x740: steering 1 ms, the others 39 ms, so an STM32 wins.
- Seed 0x209: the Pi 2 ms, the others 38 ms, so the Pi wins.

Waiting for the whole cluster matters: the Pi boots about 30 s after the
Nucleos, so without it they'd always elect each other first.

**Terms are 8 bits. What happens at 255?**
The term stops at 255 instead of wrapping to 0 (0 would look older than
everything and deadlock elections). That's 254 real elections; a cold
restart resets it. Corrupted frames once pushed us there, so Raft frames now
carry a CRC-4, and votes or acks claiming a term more than 32 ahead are
dropped.

## The log and state machine

**What's in the state machine?**
The network graph: each node up/down (3 bits), each node's link to each bus
up/down (6 bits), cockpit OK/ERROR (1 bit). It fits in 16 bits, so a
snapshot fits in one CAN frame.

**What does a log entry hold?**
(term, index, event). There are 20 one-byte events, each mapped to one
handout mutator:

| Event | Mutator |
|---|---|
| `CANLINK_X_Y_UP` / `_DOWN` | `addLink` / `dropLink` |
| `NODE_X_UP` | `addNode`: the node comes back with no links until its `addLink`s |
| `NODE_X_DOWN` | `dropNode`: also removes its links |
| `COCKPIT_OK` / `_ERROR` | leave / enter failure mode |

**Three nodes report the same failure. How many log entries?**
One. The leader appends a report only if it would change the state,
including entries still waiting to commit.

**When is an entry committed and applied?**
Once a majority (2 of 3) has it. Every node applies committed entries in
index order, so every `getNetwork()` agrees.

**Why does the system start with the cockpit in ERROR?**
It powers up in the error state (Lab 1 R3), so the first `COCKPIT_OK` is a
log entry, committed once the nodes hear the Pi.

**30 000 log entries on a Nucleo?**
`log_size` is 30 000 logical entries. A Nucleo keeps the newest 256 in RAM
plus a snapshot of the graph as of the oldest; 30 000 real entries would not
fit in 96 KB. A node that missed compacted entries gets the snapshot in one
SNAPSHOT frame.

**What did the hardware teach us about Raft?**
Standard Raft assumes logs survive a reboot. Ours are in RAM, so a rebooted
follower reports a shorter log than the leader recorded. The leader now
treats that as a reboot and starts over from what the follower has (before
this, it waited forever).

## Client and routing

**How does a node detect failures?**
From the Lab 3 heartbeats, which every node sends every 20 ms on both buses:
- **my own link Y is down** if I hear nobody on bus Y for 60 ms;
- **node X is down** if I hear it on neither bus for 200 ms (for the
  cockpit, that's also `COCKPIT_ERROR`);
- **if I hear nobody at all, I report nothing about others.** An isolated
  node can't tell whether it failed or they did.

**Why do I report my own links but not other nodes' links?**
A pulled jumper cuts both directions, so "I hear no one on A" reliably means
my A link is down. "I can't hear X on A" could be X's link or mine.

**Why 200 ms for nodes but 60 ms for links?**
After a double link fault, a node may hear another only through the relay,
which takes about 60–80 ms to start. With 60 ms for both, we committed "the
cockpit is down" while it was fine (seen on hardware).

**How does a report reach the leader?**
As an EVENT frame. A follower that gets it answers with a redirect to the
leader. The client retries every 50 ms until it gets OK or "no change".
Reports that stopped being true are withdrawn before they're sent.

**How is the routing table built from `getNetwork()`?**
- Node X's frames arriving on bus b are relayed to the other bus if some
  live node is on the other bus but not on b, and X isn't on the other bus.
- The relay is the lowest-numbered STM32 that is on both buses.
- Every node has the same committed graph, so they all agree on exactly one
  relay. This replaces Lab 3's always-on relay.

**The handout's 5.2 edge case: two links fail. What happens?**
The committed graph is stale until the second failure commits, and that
report may itself need the relay. Every heartbeat carries its sender's view
of its own links. Nodes merge that into their graph and start relaying about
one heartbeat after the cut is detected, before anything commits.
Checkoff 5 committed exactly the two expected entries.

**Why is REF special when relaying?**
REF carries a different time on each bus (each bus's own send time). If a
relay forwarded it onto a bus where the leader also sends it, the same ID
would carry different data, which is a collision (the Lab 3 bit-error
problem). So REF is relayed only when the leader is off that bus.

**Why doesn't the Pi relay?**
Linux has no worst-case timing bound, and in Lab 3 the relay carried the
brake path. Consequence: if N2 is only on A and N3 only on B, nobody can
bridge them. We've asked the TA (question 13).

## Time synchronization

**How do you get the network delay without a request/response?**
Both ends timestamp the *end of the frame on the wire*:
- the leader at transmit-complete, the followers at receive;
- STM32: an interrupt on the CAN chip's INT line;
- Pi: kernel timestamps.

The delay is then almost zero. We measured −2 µs (the receiver flags the
frame one bit before the sender does) and calibrated it out with the PPS
scope traces.

**Why does REF carry the *previous* REF's time?**
A frame can't carry its own completion time: it's only known after it's
sent. So REF k carries when REF k−1 finished, and the follower pairs that
with when *it* received REF k−1.

**How is drift detected and applied?** (report section 1)
- **Detected:** drift = (leader time difference) / (my time difference) over
  REF pairs at least 4 s apart, then filtered.
- **Applied:** `global_gettime()` = last sync point + local time since ×
  (1 + drift).

It matters when REFs stop (elections). Measured: 83 s without REF, then off
by only 6 µs; without drift correction it would have been about 360 µs.

**Why a 4 s baseline and not consecutive REFs?**
The Pi's timestamps jitter by ±40 µs. Over 20 ms that's thousands of ppm of
noise; over 1 s it's ±40–80 ppm; over 4 s about ±10 ppm.

**What happens to time when the leader changes?**
The new leader was synced, so it keeps its own global time (same offset,
same rate). The followers see no step. Measured: FL and RL 0.74 µs apart at
4 Hz after the Pi died, with no visible step.

**How are FL and RL kept in step?**
Lamps switch at global-time instants, not when a frame arrives.
- **Hazards:** on in even half-periods counted from global time 0, at 2 Hz,
  or 4 Hz in failure mode.
- **Turn signals:** BLINK carries a start time 50 ms ahead, then 500 ms
  slots.

Measured: 0.5 µs (hazards), 0–1 µs (turn signals), and 46 µs between FL and
the Pi's HZ.

**Why is the time in REF modulo 4000 s?**
A 32-bit µs counter wraps after 4294.97 s, which isn't a whole number of
seconds. Nodes that disagreed by one wrap would put their PPS edges almost a
second apart. 4000 s is a whole number of seconds and still fits in 32 bits.

**How is PPS made so precise?**
A thread sleeps until 300 µs before the edge, then busy-waits to the exact
instant (the STM32 tick is 100 µs). STM32-to-STM32 PPS spread: under 1 µs.

## What would change with four nodes?

- **Majority becomes 3 of 4.** It still tolerates only one failed node, the
  same as three, so the fourth node buys capacity, not fault tolerance.
- **An even split can stall.** A 2–2 partition (e.g. two nodes per bus after
  link faults) has no majority on either side, so nothing commits and no
  leader can be elected. With three nodes every split has a majority side.
- **Configuration:** `N = 4` in the shared config, node IDs 1–4, and the
  receive filters widened. Each per-sender ID range becomes base+1 to
  base+4, which still fits between the current bases.
- **State machine:** 4 node bits + 8 link bits + cockpit = 13 bits, so a
  snapshot still fits in one frame. Event codes go from 20 to 26.
- **Routing:** more possible relays (still the lowest-numbered STM32 on both
  buses) and more ways to partition the buses.
- **Bus load:** about 33 % more heartbeat and acknowledgement traffic, still
  far below the 50 % limit.
- **Time sync is unchanged:** one leader, everyone else follows its REF.

## Running it at the checkoff

| What | How |
|---|---|
| Watch the Pi | `ssh pi4`, then `tail -f ~/lab5-cockpit.log` |
| Restart the Pi's cockpit | `sudo systemctl restart lab5-cockpit` |
| Bus traffic (checkoff 2) | `candump -t d can0` / `can1` on the Pi. Which is bus A can swap between boots: the log's `N1: bus A = canX` line says |
| LEADER=random on the Nucleos | hold B1 while pressing RESET |
| Seed that makes the Pi win | hold B1 for 3 s or more after RESET |
| LEADER=random on the Pi | `COCKPIT_ARGS=--rt --leader=random` in `~/lab5-cockpit.env`, add `--seed=pi` for the Pi-wins seed, then restart |
| Back to normal | Pi env `--leader=cockpit`; reset the Nucleos without B1 |
| Scope pins | FL N2 D14, RL N3 D14, FR/RR D15; PPS STM32 A2, Pi pin 11; HZ Pi pin 32 |
