"""Lab 3 report Sections 3-4, following the TA clarification template.

Fill in the measurements below (us; None = not measured yet), then run
    python lab3_CAN/tools/rta_template.py --write
to regenerate everything between the RTA-BEGIN / RTA-END markers in
Doc/lab3/lab3_report.md. Without --write it prints the markdown.
Unmeasured values stay as symbols (C2, B, ...) and their results show TBD.

Model (TA clarification doc):
- Part 3 is single bus. Every C is the Part 2 placeholder except #1-#4.
- B = one SPI frame load (#4) for every task except the lowest priority:
  our send helper takes the SPI lock per bus, never across both buses.
- R = C + B + sum over higher-priority j of ceil(R/T_j)*C_j, iterated.
- CAN_RX runs once per received message: 7 messages per 20 ms per bus.
"""
import math
import pathlib
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

M = {  # measured maxima, us
    1: 126.5,   # Pi stage: UDP_RX -> CMD_TX (M1, formerly photo 6)
    2: 309.0,   # CAN_RX C, N3: INT of CAN A falling -> CAN_RX_A
    3: 147.0,   # DRIVE C, N3: CAN_RX_A -> DIR_A on a brake frame, N3 bus B unplugged (single bus).
                # With both buses: 503 us (the bus B copy's receive preempts DRIVE).
    4: 116.5,   # SPI frame load: longest CS-low window
    5: 289.5,   # CAN_RX C, N2: INT of CAN A falling -> CAN_RX_A
    6: 405.0,   # RELAY C, N2: CAN_RX_A -> RELAY_TX (after the send on bus B returns)
}
# Screenshots: Doc/assets/lab3_scope/required/m<N>.jpeg, Appendix A.1 "M<N>"

FRAME = 270.0
SERVO_PERIOD = 20000.0
MSGS = 7          # catalog messages per bus per 20 ms (single bus)
RELAYED = 6       # frames N2 forwards per 20 ms in scenario (c)

REPORT = pathlib.Path(__file__).resolve().parents[2] / "Doc" / "lab3" / "lab3_report.md"


def fmt(x):
    if x is None:
        return "TBD"
    if abs(x - round(x)) < 1e-9:
        return f"{int(round(x)):,}"
    return f"{x:,.1f}"


class V:
    """A C value that may be unmeasured: symbol + number."""

    def __init__(self, sym, num):
        self.sym, self.num = sym, num

    def show(self):
        return self.sym if self.num is None else fmt(self.num)


def meas(sym, n):
    return V(sym, M[n])


def const(x):
    return V(fmt(x), float(x))


B4 = meas("B", 4)


def term(k, c):
    return f"{k}·{c}" if k > 1 else c


def rta(C, lowest, hp):
    """hp: [(C_j V, T_j, count)]. Returns (R, work)."""
    b_sym = "0" if lowest else "B"
    if C.num is None or (not lowest and B4.num is None) or any(c.num is None for c, _, _ in hp):
        sym = " + ".join([C.sym, b_sym] + [term(k, c.sym) for c, _, k in hp])
        return None, sym
    b = 0.0 if lowest else B4.num
    R = C.num + b
    for _ in range(200):
        nR = C.num + b + sum(math.ceil(R / T - 1e-9) * k * c.num for c, T, k in hp)
        if abs(nR - R) < 1e-9:
            break
        R = nR
    mult = [math.ceil(R / T - 1e-9) for _, T, _ in hp]
    if all(m == 1 for m in mult):
        work = " + ".join([fmt(C.num), fmt(b)] + [term(k, fmt(c.num)) for c, _, k in hp])
    else:
        work = " + ".join([fmt(C.num), fmt(b)] +
                          [f"⌈R/{fmt(T)}⌉·{term(k, fmt(c.num))}" for c, T, k in hp])
    return R, f"{work} = **{fmt(R)}**"


def node(rows):
    """rows (priority order, highest first): (task, prio, C, T_us, T_text, D_us, D_text, count)."""
    lines = ["| Task | Priority | C (µs) | T | D | B (µs) | R (µs) | R ≤ D? | Work: C + B + higher-priority C |",
             "|---|---|---:|---|---|---:|---:|---|---|"]
    res = {}
    for i, (task, prio, C, T, Ttxt, D, Dtxt, k) in enumerate(rows):
        lowest = i == len(rows) - 1
        R, work = rta(C, lowest, [(r[2], r[3], r[7]) for r in rows[:i]])
        res[task] = R
        if D is None:
            ok = "see chain"
        elif R is None:
            ok = "TBD"
        else:
            ok = "Yes" if R <= D else "**No**"
        lines.append(f"| {task} | {prio} | {C.show()} | {Ttxt} | {Dtxt} | "
                     f"{'0 (lowest)' if lowest else B4.show()} | {fmt(R)} | {ok} | {work} |")
    return "\n".join(lines), res


def add(*xs):
    return None if any(x is None for x in xs) else sum(xs)


def verdict(total, deadline, why, fix):
    if total is None:
        return "TBD"
    if total <= deadline:
        return "**Met**"
    return f"**Missed.** {why} Fix: {fix}"


def build():
    C2, C3, C5, C6 = meas("C2", 2), meas("C3", 3), meas("C5", 5), meas("C6", 6)
    pi = M[1]
    out = []
    w = out.append

    n3_rows = [
        ("CAN_RX", "coop 2", C2, 20000, "20 ms per message, 7 per bus", 300, "0.3 ms", MSGS),
        ("DRIVE", "2", C3, 20000, "20 ms", 500, "0.5 ms", 1),
        ("BLINK_R", "3", const(20), 20000, "20 ms", 500, "0.5 ms", 1),
        ("TEST_BTN", "4", const(20), 50000, "50 ms", 10000, "10 ms", 1),
        ("PID", "5", const(200), 10000, "10 ms", 10000, "10 ms", 1),
        ("HB_MON", "6", const(30), 20000, "20 ms", 20000, "20 ms", 1),
        ("HB_TX", "7", const(120), 20000, "20 ms", 20000, "20 ms", 1),
    ]
    n2_rows = [
        ("CAN_RX", "coop 2", const(80), 20000, "20 ms per message, 7 per bus", 300, "0.3 ms", MSGS),
        ("STEER", "2", const(50), 20000, "20 ms", 1000, "1 ms", 1),
        ("BLINK_F", "3", const(20), 20000, "20 ms", 500, "0.5 ms", 1),
        ("TEST_BTN", "4", const(20), 50000, "50 ms", 10000, "10 ms", 1),
        ("HB_MON", "6", const(30), 20000, "20 ms", 20000, "20 ms", 1),
        ("HB_TX", "7", const(120), 20000, "20 ms", 20000, "20 ms", 1),
        ("FORCE_TX", "8", const(120), 50000, "50 ms", 50000, "50 ms", 1),
    ]
    t3, r3 = node(n3_rows)
    t2, r2 = node(n2_rows)

    # ---------------- Section 3 ----------------
    w("## 3. Single-bus real-time analysis\n")
    w("Per the TA clarification:")
    w("- **Scope:** one bus, no redundancy or relay (Section 4).")
    w("- **C:** the handout's Part 2 placeholders, except the four measured values below.")
    w("- **Priorities:** as implemented (`common/zephyr/zone_config.h`). CAN_RX is the MCP2515 driver's")
    w("  cooperative thread, so it outranks every application task. The per-bus CAN_TX threads are folded")
    w("  into HB_TX and FORCE_TX C (120 µs, both buses).")
    w("- **B = #4 (one SPI frame load)** for every task but the lowest (B = 0). Not 2 × #4: the SPI lock is")
    w("  held for one transaction at a time.")
    w("- **R** = C + B + Σ over higher-priority j of ⌈R/T_j⌉·C_j. Every R is far below 10 ms, so each")
    w("  ⌈R/T_j⌉ is 1, except CAN_RX, which runs once per message: 7 per 20 ms on one bus.")
    w("")
    w("### 3.1 Measurements\n")
    w("Scope maxima over at least a minute of traffic, other tasks idle. Test points are GPIO writes of a")
    w("few µs. Screenshots: Appendix A.1.\n")
    w("| # | Number | Start probe → stop probe | Max (µs) | Screenshot |")
    w("|---|---|---|---:|---|")
    desc = {1: ("Pi stage", "N1 UDP_RX (after the socket read) → CMD_TX (after the CAN writes)"),
            2: ("CAN_RX C, N3", "N3 CAN A INT (PA10 / D2) falling → CAN_RX_A"),
            3: ("DRIVE C, N3", "N3 CAN_RX_A → DIR_A on a brake frame, bus B unplugged (single bus). With both buses: 503 µs, because the bus B copy's driver thread preempts DRIVE for ~300 µs (M2)"),
            4: ("SPI frame load (B)", "MCP2515 CS (PB6 / D10) low → high, longest window")}
    for n in (1, 2, 3, 4):
        name, how = desc[n]
        w(f"| {n} | {name} | {how} | {fmt(M[n])} | M{n} |")
    w("")
    w("### 3.2 Node RTA: N3 (drivetrain)\n")
    w(t3 + "\n")
    w("### 3.3 Node RTA: N2 (steering)\n")
    w(t2 + "\n")
    w("**Why the No rows miss:** CAN_RX outranks every application task and is charged 7 times, as if all 7")
    w("messages arrived inside the window. One bus delivers at most ⌈R / 270 µs⌉ frames in R, so that bound")
    w("would remove most misses; so would a shorter receive path or raising the 0.5 ms tasks above CAN_RX.\n")

    R_ctrl = pi + FRAME + FRAME
    bus_ctrl = FRAME + FRAME
    bus_blink = (FRAME + 5 * FRAME) + FRAME
    w("### 3.4 Message RTA: CTRL (same on both buses)\n")
    w("| Message | J (µs) | C (µs) | B (µs) | w (µs) | R (µs) |")
    w("|---|---:|---:|---:|---:|---:|")
    w(f"| 0x010 CTRL | {fmt(pi)} (#1) | 270 | 270 | 270 | **{fmt(R_ctrl)}** |\n")
    w("- **w** = 270 µs: CTRL has the lowest ID, so only one lower-priority frame already on the wire blocks it.")
    w(f"  R = J + w + C = {fmt(pi)} + 270 + 270 = {fmt(R_ctrl)} µs; \"CTRL on bus\" in the chains is w + C = 540 µs.")
    w("- **BLINK** has 5 higher-priority IDs (CTRL, FAULT, HB_N1–3): w = 270 + 5·270 = 1,620 µs, so")
    w(f"  BLINK on bus = {fmt(bus_blink)} µs.\n")

    brake = add(pi, bus_ctrl, r3["CAN_RX"], r3["DRIVE"])
    steer = add(pi, bus_ctrl, r2["CAN_RX"], r2["STEER"], SERVO_PERIOD)
    blink = add(pi, bus_blink, r3["CAN_RX"], r3["BLINK_R"])
    n3_path = add(r3["CAN_RX"], r3["BLINK_R"])
    n2_path = add(r2["CAN_RX"], r2["BLINK_F"])
    n3_best = add(C2.num, 20.0)
    n2_best = 80.0 + 20.0
    sync = None if n3_path is None or n2_path is None else max(n3_path - n2_best, n2_path - n3_best)
    err = add(r2["TEST_BTN"], 66000, 20000, r3["HB_MON"])
    w("### 3.5 End-to-end chains (single bus)\n")
    w("| Chain | Stages to add up (µs) | Total | Deadline | Met? |")
    w("|---|---|---:|---:|---|")
    w(f"| Brake / throttle | Pi {fmt(pi)} + CTRL on bus 540 + CAN_RX (N3) {fmt(r3['CAN_RX'])} + DRIVE {fmt(r3['DRIVE'])} | "
      f"{fmt(brake)} | 2 ms | {verdict(brake, 2000, 'DRIVE carries 7 CAN_RX interferences on top of its own receive path.', 'a tighter arrival bound or a shorter receive path (Appendix B).')} |")
    w(f"| Steering | Pi {fmt(pi)} + CTRL on bus 540 + CAN_RX (N2) {fmt(r2['CAN_RX'])} + STEER {fmt(r2['STEER'])} + one servo period 20,000 | "
      f"{fmt(steer)} | 50 ms | {verdict(steer, 50000, '', '')} |")
    w(f"| Blinker start | Pi {fmt(pi)} + BLINK on bus {fmt(bus_blink)} + CAN_RX (N3) {fmt(r3['CAN_RX'])} + BLINK_R {fmt(r3['BLINK_R'])} | "
      f"{fmt(blink)} | 100 ms | {verdict(blink, 100000, '', '')} |")
    w(f"| Blinker sync | Worst case of one receive path minus the other's best case (C only): "
      f"N3 {fmt(n3_path)} − N2 {fmt(n2_best)}, and N2 {fmt(n2_path)} − N3 {fmt(n3_best)} | "
      f"{fmt(sync)} | 1 ms | {verdict(sync, 1000, 'The two zones see different interference after the same frame.', 'lower CAN_RX C, or a tighter arrival bound.')} |")
    w(f"| Error propagation | TEST_BTN (N2) {fmt(r2['TEST_BTN'])} + 3 missed heartbeats 66,000 + one HB_MON period 20,000 + HB_MON (N3) {fmt(r3['HB_MON'])} | "
      f"{fmt(err)} | 100 ms | {verdict(err, 100000, '', '')} |\n")
    w("- **Blinker sync:** both STM32s get the same BLINK frame at the same instant, so the offset is the")
    w("  difference of their receive-to-lamp paths: worst minus best, taken both ways.")
    w("- **Error propagation** uses heartbeats; a test-button FAULT frame reached the other zone in 1.1 ms (photo 13).\n")

    # ---------------- Section 4 ----------------
    n2r_rows = [
        ("CAN_RX", "coop 2", C5, 20000, "20 ms per message, 7 per bus × 2", 300, "0.3 ms", 2 * MSGS),
        ("RELAY", "1", C6, 20000, "6 forwarded per 20 ms in (c)", None, "brake chain", RELAYED),
        ("STEER", "2", const(50), 20000, "20 ms", 1000, "1 ms", 1),
        ("BLINK_F", "3", const(20), 20000, "20 ms", 500, "0.5 ms", 1),
        ("TEST_BTN", "4", const(20), 50000, "50 ms", 10000, "10 ms", 1),
        ("HB_MON", "6", const(30), 20000, "20 ms", 20000, "20 ms", 1),
        ("HB_TX", "7", const(120), 20000, "20 ms", 20000, "20 ms", 1),
        ("FORCE_TX", "8", const(120), 50000, "50 ms", 50000, "50 ms", 1),
    ]
    t2r, r2r = node(n2r_rows)

    w("## 4. Dual-bus and relay analysis\n")
    w("### 4.1 Hot/parallel transmit\n")
    w("- **Send:** each message goes on A then B with one sequence number (`can_link_send`).")
    w("- **Receive:** the first copy by `(ID, seq)` is used and the second dropped (FAULT: one dedup slot per sender).")
    w("- **Cost:** two interrupts and SPI reads per message, so CAN_RX runs 14 times per 20 ms instead of 7 (4.4).")
    w("- **Chains get no worse:** receivers act on the first copy, and both copies leave together (the Pi writes")
    w("  A then B inside the measured #1 stage).\n")
    w("### 4.2 Relay design\n")
    w("| Question | Our decision | Why |")
    w("|---|---|---|")
    w("| Who relays? | **N2 and N3**, the two STM32s; never the Pi | The four checkoff cases need N2 (N1/N3 on opposite buses) or N3 (N1/N2). A Pi relay would put unbounded Linux in the path. |")
    w("| When? | **Always**, with no fault detection | Waiting for the 60 ms degraded detection would cut CTRL (brakes included) for 60–70 ms. It costs bus time instead (4.5). |")
    w("| What? | The **first copy** of every frame the zone didn't send itself, onto the other bus | A node already sends its own frames on both buses. |")
    w("| Loops? | **Dedup, no flag:** only the first copy of each `(ID, seq)`, never own frames, forwarded **unchanged** | Each zone forwards a frame at most once, so forwarding stops. An earlier \"relayed\" bit made the copy differ from the sender's same-ID frame; when both arbitrated together, both saw bit errors (TEC ~76). Identical frames sent together are legal CAN. |")
    w("| RELAY priority? | **1**: above STEER and all application tasks; below the driver and CAN_TX threads | It is on the relayed brake path. The receive hook only queues (16 deep); ID-sorted TX queues let a relayed CTRL overtake HB/FORCE. |")
    w("| Cost to N2 | 6 RELAY jobs per 20 ms in (c) | In 4.4. |\n")
    w("N2 relays when N1 and N3 are on opposite buses; N3 relays when N1 and N2 are. We analyze N1 off B +")
    w("N3 off A, as the clarification asks.\n")
    w("### 4.3 Measurements\n")
    w("| # | Number | Start probe → stop probe | Max (µs) | Screenshot |")
    w("|---|---|---|---:|---|")
    w(f"| 5 | CAN_RX C, N2 | N2 CAN A INT (PA10 / D2) falling → CAN_RX_A | {fmt(M[5])} | M5 |")
    w(f"| 6 | RELAY C, N2 | N2 CAN_RX_A → RELAY_TX (pulsed after the relayed `can_send()` on B returns); N1 off B, N3 off A | {fmt(M[6])} | M6 |\n")
    w("### 4.4 Re-analysis: N2 with RELAY\n")
    w(t2r + "\n")
    w("### 4.5 Bus utilization with our relay design\n")
    w("From the template's 8.6 % (6 frames per 20 ms + FORCE per 50 ms), each frame relayed every 20 ms adds 1.35 %.\n")
    w("| Bus | Relayed onto it in scenario (c) | New utilization | ≤ 50 %? |")
    w("|---|---|---:|---|")
    w("| A | HB_N3', FAULT_N3' (N3's frames, from B), each 20 ms: 8.6 + 2 × 1.35 | **11.3 %** | Yes |")
    w("| B | CTRL', HB_N1', BLINK', FAULT_N1' (N1's frames, from A), each 20 ms: 8.6 + 4 × 1.35 | **14.0 %** | Yes |\n")
    w("**Worst case is normal operation** (both buses up): N2 forwards N1's 4 and N3's 2 frames per 20 ms; N3")
    w("forwards N1's 4, HB_N2, FAULT_N2 and FORCE. All on one bus: 8.6 + 12 × 1.35 + 0.54 = **25.3 %** < 50 %.\n")
    J_rel = add(pi, 540, r2r["CAN_RX"], r2r["RELAY"])
    R_rel = add(J_rel, FRAME, FRAME)
    w("### 4.6 Relayed CTRL on bus B\n")
    w("| Message | J (µs) | C (µs) | B (µs) | w (µs) | R (µs) |")
    w("|---|---|---:|---:|---:|---:|")
    w(f"| 0x010 CTRL, relayed | Pi {fmt(pi)} + CTRL on A 540 + CAN_RX R (N2) {fmt(r2r['CAN_RX'])} + RELAY R (N2) {fmt(r2r['RELAY'])} = **{fmt(J_rel)}** | 270 | 270 | 270 | **{fmt(R_rel)}** |\n")
    w("J is the whole first hop. The relayed CTRL still has the lowest ID on B, so R = J + 540; the brake chain")
    w("adds CAN_RX (N3) + DRIVE to this R.\n")

    rel_brake = add(R_rel, r3["CAN_RX"], r3["DRIVE"])
    steer_c = add(pi, bus_ctrl, r2r["CAN_RX"], r2r["STEER"], SERVO_PERIOD)
    blink_c = add(pi, bus_blink, r2r["CAN_RX"], r2r["RELAY"], bus_blink, r3["CAN_RX"], r3["BLINK_R"])
    n3_rel = add(r2r["RELAY"], bus_blink, r3["CAN_RX"], r3["BLINK_R"])
    sync_c = None if n3_rel is None or r2r["CAN_RX"] is None else max(n3_rel - n2_best,
                                                                         r2r["CAN_RX"] + r2r["BLINK_F"] - n3_best)
    err_c = add(r2r["TEST_BTN"], 66000, 20000, r3["HB_MON"])
    w("### 4.7 Chains in scenario (c): N1 off bus B, N3 off bus A\n")
    w("| Chain | Stages to add up (µs) | Total | Deadline | Met? | What changed |")
    w("|---|---|---:|---:|---|---|")
    w(f"| Brake / throttle | relayed CTRL R (4.6) {fmt(R_rel)} + CAN_RX (N3) {fmt(r3['CAN_RX'])} + DRIVE {fmt(r3['DRIVE'])} | "
      f"{fmt(rel_brake)} | 2 ms | {verdict(rel_brake, 2000, 'Extra hop: N2 receive + RELAY + second bus.', 'accept degraded latency in this double fault (handout option).')} | Second bus hop via N2's RELAY |")
    w(f"| Steering | Pi + CTRL on A 540 + CAN_RX (N2) {fmt(r2r['CAN_RX'])} + STEER {fmt(r2r['STEER'])} + servo period 20,000 | "
      f"{fmt(steer_c)} | 50 ms | {verdict(steer_c, 50000, '', '')} | N2 still gets CTRL on A; only its load grew |")
    w(f"| Blinker start | Pi + BLINK on A {fmt(bus_blink)} + CAN_RX (N2) + RELAY + BLINK' on B {fmt(bus_blink)} + CAN_RX (N3) + BLINK_R | "
      f"{fmt(blink_c)} | 100 ms | {verdict(blink_c, 100000, '', '')} | N3 gets BLINK only through the relay |")
    w(f"| Blinker sync | RELAY + BLINK' on B + CAN_RX (N3) + BLINK_R − N2's best case | "
      f"{fmt(sync_c)} | 1 ms | {verdict(sync_c, 1000, 'N3 acts on the relayed copy, one hop after N2.', 'accept as degraded in (c).')} | N3's edge trails by the relay hop |")
    w(f"| Error propagation | TEST_BTN (N2) {fmt(r2r['TEST_BTN'])} + 66,000 + 20,000 + HB_MON (N3) {fmt(r3['HB_MON'])} | "
      f"{fmt(err_c)} | 100 ms | {verdict(err_c, 100000, '', '')} | HB_N1 reaches N3 only via the relay |\n")
    return "\n".join(out)


def link_screenshots(s):
    """Anchor every Appendix A screenshot and link each mention to it.

    Anchors: <a id="mN"> before each M1-M6 heading, <a id="photo-N"> before
    each additional capture (found from its photoNN.jpeg image line).
    Links: "M3" -> [M3](#m3), "photo 13" -> [photo 13](#photo-13),
    "photos 16–17" -> photos [16](#photo-16)–[17](#photo-17).
    Headings, image lines and existing links are left alone, so running it
    again changes nothing.
    """
    lines = s.split("\n")
    out = []
    for i, line in enumerate(lines):
        nxt = lines[i + 1] if i + 1 < len(lines) else ""
        prev = out[-1] if out else ""
        if line.startswith("#### "):
            m = re.match(r"#### M([1-6]) — ", line)
            p = re.search(r"/photo(\d+)\.jpeg\)", nxt)
            # M1 is the old photo 6, so "photo 6" links land on it too
            anchor = ('<a id="m1"></a><a id="photo-6"></a>' if m and m.group(1) == "1" else
                      f'<a id="m{m.group(1)}"></a>' if m else
                      f'<a id="photo-{int(p.group(1))}"></a>' if p else None)
            if anchor and prev != anchor:
                out.append(anchor)
            out.append(line)
            continue
        if line.startswith(("#", "![", "<a id=")):
            out.append(line)
            continue
        line = re.sub(r"(?<![\[\w#-])M([1-6])\b(?!\]|\s—|:)",
                      lambda m: f"[M{m.group(1)}](#m{m.group(1)})", line)
        line = re.sub(r"(?<![\[\w])([Pp]hotos) (\d+)–(\d+)\b(?!\]\()",
                      lambda m: f"{m.group(1)} [{m.group(2)}](#photo-{m.group(2)})–[{m.group(3)}](#photo-{m.group(3)})",
                      line)
        line = re.sub(r"(?<![\[\w])([Pp]hoto) (\d+)\b(?![–\d]|\]\()",
                      lambda m: f"[{m.group(1)} {m.group(2)}](#photo-{m.group(2)})", line)
        out.append(line)
    return "\n".join(out)


def main():
    md = build()
    if "--write" in sys.argv:
        s = REPORT.read_text(encoding="utf-8")
        new, n = re.subn(r"<!-- RTA-BEGIN -->.*<!-- RTA-END -->",
                         lambda _: "<!-- RTA-BEGIN -->\n" + md + "\n<!-- RTA-END -->", s, flags=re.S)
        if n != 1:
            sys.exit("RTA markers not found in the report")
        # Appendix A captions for measurements #2-#6
        for k in (2, 3, 4, 5, 6):
            new = re.sub(rf"(#### M{k} — Measurement #{k}: [^\n]*?\)\. Max )[^\n]*",
                         lambda m, k=k: m.group(1) + (f"{fmt(M[k])} µs" if M[k] is not None else "TBD"), new)
        new = link_screenshots(new)
        REPORT.write_text(new, encoding="utf-8")
        print(f"updated {REPORT}")
    else:
        print(md)


if __name__ == "__main__":
    main()
