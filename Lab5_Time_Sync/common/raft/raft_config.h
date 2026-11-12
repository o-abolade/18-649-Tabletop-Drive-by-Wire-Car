/*
 * Lab 5 Raft cluster configuration (handout Part 2).
 *
 * This header is "the config file": every node (both STM32 zones and the Pi)
 * compiles the same file, so the configuration is identical everywhere. The
 * Nucleos have no filesystem, so a shared header stands in for a file
 * (Progress_doc_lab5.md, question 9).
 *
 * Plain C99, no Zephyr or Linux dependencies. A build may override a key with
 * -D (e.g. -DRAFT_DEFAULT_LEADER=RAFT_LEADER_NONE for LEADER=random), but must
 * then do so for all three nodes.
 */
#ifndef RAFT_CONFIG_H
#define RAFT_CONFIG_H

/* nodes: one ID per node, the same numbering as the Lab 3 zones (canp_zone) */
#define RAFT_NODE_COCKPIT     1   /* N1, Raspberry Pi */
#define RAFT_NODE_STEERING    2   /* N2, STM32 front */
#define RAFT_NODE_DRIVETRAIN  3   /* N3, STM32 rear */

/* N: cluster size. Majority = N/2 + 1 = 2. */
#define RAFT_N                3
#define RAFT_MAJORITY         (RAFT_N / 2 + 1)

/* Number of CAN buses (A = CAN 1, B = CAN 2 in the handout's figure). */
#define RAFT_NUM_BUSES        2

/* default_leader: node ID of the predefined leader, or RAFT_LEADER_NONE.
 * With a node ID, every node boots in term 1 with that leader and no
 * election is held. LEADER=cockpit / LEADER=random in the checkoff. */
#define RAFT_LEADER_NONE      0
#ifndef RAFT_DEFAULT_LEADER
#define RAFT_DEFAULT_LEADER   RAFT_NODE_COCKPIT
#endif

/* heartbeat_ms: leader heartbeat (REF) period, also the clock-sync period. */
#define RAFT_HEARTBEAT_MS     20

/* missed_heartbeats: consecutive REFs a follower may miss before it starts
 * an election. */
#define RAFT_MISSED_HEARTBEATS 3

/* election_timeout_jitter_ms: timeout = missed_heartbeats * heartbeat_ms
 * + U(0, jitter). */
#define RAFT_ELECTION_JITTER_MS 39
#define RAFT_ELECTION_BASE_MS (RAFT_MISSED_HEARTBEATS * RAFT_HEARTBEAT_MS)

/* Empty APPEND (commit index carrier) period when nothing new is appended
 * (checkoff step 2). */
#define RAFT_EMPTY_APPEND_MS  100

/* log_size: number of events the log holds (logical indexes 1..log_size).
 * The STM32s keep only the most recent RAFT_LOG_WINDOW entries in RAM plus a
 * snapshot of the applied state (question 8). */
#define RAFT_LOG_SIZE         30000
#ifndef RAFT_LOG_WINDOW
#define RAFT_LOG_WINDOW       256
#endif

/* Seed for the election jitter in LEADER=random mode; each node mixes in its
 * own ID so the three nodes draw different timeouts from one seed. */
#ifndef RAFT_SEED
#define RAFT_SEED             0x5EEDu
#endif

/* Debug: print election and append messages. */
#ifndef RAFT_DEBUG
#define RAFT_DEBUG            1
#endif

/* Nodes allowed to relay between buses. The Pi never relays: Linux has no
 * worst-case bound (Lab 3 report, 4.2). Bit n = node n. */
#define RAFT_RELAY_CAPABLE    ((1u << RAFT_NODE_STEERING) | (1u << RAFT_NODE_DRIVETRAIN))

#endif /* RAFT_CONFIG_H */
