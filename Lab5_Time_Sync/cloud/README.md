# Lab 5 extra credit: cloud node with Redis and a GUI

The vehicle's Raft log is the source of truth. The cloud node mirrors it so
someone can watch the vehicle from a browser. The Pi is the only node with a
network interface, so it is the uplink.

```
 Pi (N1) cockpit                         cloud VM (Docker)
  Raft apply ─▶ uplink queue (1000)        Redis   127.0.0.1:6379, password
  1 s status ─▶ uplink thread  ── SSH ──▶     ▲
               (nice 10, CPUs 0-1) tunnel      │
                                           web app (FastAPI, login, websocket)
                                               ▲
                                           Caddy  :443 HTTPS ◀── browser
```

| Redis key | Written | Holds |
|---|---|---|
| `vehicle:log` (stream, XADD) | every committed entry | run, term, index, type, code, replayed, global time, Pi wall clock |
| `vehicle:heartbeat` (hash, TTL 5 s) | every 1 s | Pi wall clock, global time, role/term/leader/commit, held entries, cockpit sync error and drift, network state |
| `vehicle:last` | every 1 s | the same without a TTL, shown greyed out while stale |
| `vehicle:index` | every committed entry | index → term of what is already in the stream |
| `vehicle:uplink` | every committed entry | last acknowledged index and stream id |

The GUI shows five views:
1. the three nodes alive or dead;
2. bus A/B for each node;
3. the leader and time master;
4. the events the leader is holding for the cockpit, and how many were
   replayed when it returned;
5. the committed log, with replayed entries marked.

A staleness banner appears when the heartbeat is more than 3 s old or has
expired. The age comes from the heartbeat key's remaining TTL, so only the
server's clock is used. The Pi has no real-time clock: after a reboot its wall
clock lags by minutes until NTP corrects it, and judging by the Pi's own
timestamp kept the banner up for 18 s after the data was back.

**FL/RL offset as the Pi measured it:** the Pi cannot see the STM32 lamps, so
the heartbeat carries the Pi's own sync figures (last REF error, drift). The
FL/RL offsets are measured on the scope (`Progress_doc_lab5.md`).

## Security

- **Redis is never reachable from the internet.**
  - It listens on the VM's 127.0.0.1 only, and requires a password.
  - The Pi reaches it through an SSH tunnel as user `vtunnel`, whose key may
    only forward to `127.0.0.1:6379`: no shell, no other ports
    (`restrict,port-forwarding,permitopen=...`).
- **HTTPS with a login.**
  - Caddy serves the GUI over HTTPS with a Let's Encrypt certificate for
    `<ip>.sslip.io`, so no domain needs buying.
  - Every page, the API and the websocket require a login. Session cookies
    are `Secure`, `HttpOnly` and `SameSite=Strict`.
  - A wrong password costs 1 s.
- **Firewall:** only 22, 80 (needed for the certificate) and 443 are open.
- **Secrets:** generated per VM into `.env` (mode 600, never committed).

## Never delaying the vehicle

The cockpit's Raft apply callback only copies the entry into a queue under a
mutex, which takes microseconds. A separate thread does all network I/O:
SCHED_OTHER at nice 10, pinned to CPUs 0–1. The cockpit loop is SCHED_FIFO 80
on CPU 3 and PPS runs on CPU 2. So REF, APPEND and CTRL never wait for the
uplink.

To show this on the scope, trigger on `CMD_TX` (Pi pin 13, CTRL out) or the
`CAN_TX_A` test point. Compare CTRL timing with the uplink running and with
the tunnel stopped (`sudo systemctl stop lab5-uplink-tunnel`).

If the uplink is down, the queue keeps the last 1000 entries and the GUI shows
the staleness banner. On reconnect the thread resumes after the last
acknowledged entry. A restarted Pi checks `vehicle:index` before adding
anything, so only the entries the leader held while the Pi was away are
added, marked `REPLAYED`.

## Setting it up

**1. The VM** (any provider; Azure for Students needs no credit card):
- Ubuntu 24.04, size B1s / t3.micro / e2-micro.
- Allow inbound **TCP 22, 80 and 443** in the cloud firewall (network
  security group).
- Note the public IP.

**2. On the Pi:** make the tunnel key.
```
cd ~/18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/pi_cockpit
./install_uplink.sh key            # prints: ssh-ed25519 AAAA... lab5-uplink
```

**3. On the VM:** install everything.
```
git clone https://github.com/o-abolade/18-649-Tabletop-Drive-by-Wire-Car.git
cd 18-649-Tabletop-Drive-by-Wire-Car/Lab5_Time_Sync/cloud
./install_vm.sh "ssh-ed25519 AAAA... lab5-uplink"
```
It prints the GUI address and login, the VM IP and the Redis password.

**4. On the Pi:** connect.
```
./install_uplink.sh <VM IP> <Redis password>
```

**5. Open** `https://<ip-with-dashes>.sslip.io` and log in.

## Demo (handout: "run the Part 5 checkpoint with the GUI open")

1. All three nodes up, the Pi leading (`LEADER=cockpit`). The GUI shows three
   live nodes, the cockpit marked LEADER and TIME MASTER, and the log.
2. **Power off the Pi.**
   - The GUI goes stale within 5 s and keeps the last known state greyed out.
   - On the vehicle, an STM32 takes over and commits `NODE_cockpit_DOWN` and
     `COCKPIT_ERROR`. Those entries are held by the new leader; the GUI can't
     see them, because the cockpit is the only uplink.
3. **Power the Pi back on.**
   - It rejoins as a follower, and the leader replays the held entries.
   - The GUI's held-event count drains to 0, and the held entries appear in
     the log marked `REPLAYED`. Entries the GUI already had are not
     duplicated.
   - The new leader is marked, the cockpit is alive again, and the banner
     clears.

## Keeping the GUI alive through a cockpit failure (the requested paragraph)

The GUI goes stale during a cockpit failure because the cockpit is the
vehicle's only uplink: every committed entry reaches the cloud through the
Pi. Keeping the GUI alive needs a second, independent uplink:
- **What it would be:** a node with its own network interface that receives
  the committed log over CAN, such as a small telematics gateway with
  Wi-Fi, Ethernet or a cellular modem, attached to both buses as a
  non-voting Raft learner.
- **How it would publish:** to the same Redis. The mirror already
  deduplicates by (index, term) in `vehicle:index`, so two uplinks can
  publish the same entries without duplicates, and the GUI would see the
  STM32 take over live.

**The Nucleos cannot be that uplink:**
- They have no network hardware, only CAN and the ST-Link USB serial port.
- They have 96 KB of RAM, which a TCP/TLS stack and an SSH or TLS session
  would mostly consume.
- They are the safety-critical steering and drivetrain zones, with hard
  deadlines. Adding unbounded network I/O to them would break the timing
  analysis that keeps braking and steering within their deadlines.
