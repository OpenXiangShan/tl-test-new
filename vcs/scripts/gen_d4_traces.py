#!/usr/bin/env python3
"""Generate the d4-* directed traces (first no-blocker batch from
docs/l2-coverage-map.md section 5).

Geometry (TestTopMatrix): L2 512 sets x 8 ways, 64B block.
  - L2 set index = addr[14:6];  lines of one set = base + n*0x8000 (n<8)
  - L3 openLLC 4096 sets x 16 ways; L3 set = addr[17:6]; a 0x40000 stride
    keeps BOTH the L2 set and the L3 set constant (the trick the
    directed2-*-snoop traces use to stack pressure onto one L3 set).
Snoop induction recipe (from directed2-clean-snoop): fill an L3 set with
16 distinct lines, hold 8 of them in L2, then push ONE fresh line
(n = 16+round) per round so L3 evicts exactly one victim; refresh the
holders each round so some victims are still L2-resident when the
back-invalidate snoop fires.

Agents (tl_ul = 0 configs): 0 = CAgent(l1d), 1 = MAgent(matrix).
Ops: LD=get S, CR=acquire T, ST=write, EV=voluntary release,
     MR=matrix read (Get), MW=matrix write (PutFullData, L2-dirty).
"""

from pathlib import Path

TRACES = Path(__file__).resolve().parents[2] / "traces"


def emit(name, header, phases):
    """phases: list of (comment, [ (op, addr, agent), ... ])"""
    out = [f"# {header}"]
    for comment, ops in phases:
        out.append(f"# {comment}")
        for op, addr, agent in ops:
            out.append(f"{op} 0x{addr:012x} {agent}")
        out.append("FE 0x000000000000 0")
    (TRACES / f"{name}.ops").write_text("\n".join(out) + "\n")
    n = sum(len(ops) for _, ops in phases)
    print(f"{name}.ops: {len(phases)} phases, {n} ops (+{len(phases)} FE)")


def fe():  # sentinel handled by emit; placeholder for readability
    raise NotImplementedError


L2_STRIDE = 0x8000      # 512 sets * 64B
L3_STRIDE = 0x40000     # 4096 sets * 64B


def set_lines(base, ns):
    return [base + n * L3_STRIDE for n in ns]


def trace_promote_burst():
    """F02: acquire S then T on the same lines -> promote on hit
    (acquire_on_hit_s3, BRANCH->TRUNK). Read back on T, then release."""
    sets = [s * 0x40 for s in (0x10, 0x30, 0x50, 0x70)]
    lines = [b + n * L2_STRIDE for b in sets for n in range(8)]
    phases = [
        ("phase 1: acquire S on 32 lines (4 sets x 8 ways)",
         [("LD", a, 0) for a in lines]),
        ("phase 2: acquire T on hit -> promote BRANCH->TRUNK",
         [("CR", a, 0) for a in lines]),
        ("phase 3: mixed hits on T (LD stays T, CR re-ask T)",
         [("LD" if i % 2 == 0 else "CR", a, 0) for i, a in enumerate(lines)]),
        ("phase 4: readback on T then release",
         [("CR", a, 0) for a in lines] + [("EV", a, 0) for a in lines]),
    ]
    emit("d4-promote-burst",
         "F02 promote on hit: S then T on the same lines, mixed hits, release.",
         phases)


def trace_put_miss_burst():
    """F05/F07: cold PutFullData misses in bursts of varying depth
    (putBuf depth + miss occupancy), then read back through MR."""
    s1 = 0x90 * 0x40
    s2 = 0xB0 * 0x40
    s3 = 0xD0 * 0x40
    burst16 = [s1 + n * L2_STRIDE for n in range(16)]          # one set, >8 ways
    burst32 = [ (s2 if i % 2 == 0 else s3) + (i // 2) * L2_STRIDE
                for i in range(32) ]                            # two sets interleaved
    trickle = [0xF0 * 0x40 + n * L2_STRIDE for n in range(8)]   # fenced one-by-one
    phases = [
        ("phase 1: burst of 16 PutFullData into one set (8 ways + in-flight)",
         [("MW", a, 1) for a in burst16]),
        ("phase 2: 32 PutFullData interleaved over two sets, no fence",
         [("MW", a, 1) for a in burst32]),
        ("phase 3: trickle baseline, one PutFullData per fence",
         [("MW", a, 1) for a in trickle]),
        ("phase 4: read every line back through the matrix port",
         [("MR", a, 1) for a in burst16 + burst32 + trickle]),
    ]
    emit("d4-put-miss-burst",
         "F05/F07 cold PutFullData miss bursts (putBuf depth + occupancy) with MR readback.",
         phases)


def trace_merge_window():
    """F06: same-address requests while the miss is outstanding.
    Same op repeated, op upgrades, cross-agent, and alternating distinct
    addresses as the no-merge control."""
    a = [0x110 * 0x40 + k * 0x40 for k in range(4)] + [0x120 * 0x40]
    phases = [
        ("phase 1: 8x LD one cold address while the miss is in flight",
         [("LD", a[0], 0)] * 8),
        ("phase 2: op upgrade under the same miss window LD->CR->ST",
         [("LD", a[1], 0)] * 4 + [("CR", a[1], 0)] * 4 + [("ST", a[1], 0)] * 4),
        ("phase 3: cross-agent MR (matrix) then LD (l1d) on one address",
         [("MR", a[2], 1)] * 4 + [("LD", a[2], 0)] * 4),
        ("phase 4: alternating distinct addresses, the no-merge control",
         [("LD" if i % 2 == 0 else "MR", a[3] if i % 2 == 0 else a[4],
           0 if i % 2 == 0 else 1) for i in range(8)]),
        ("phase 5: cleanup releases",
         [("EV", a[0], 0), ("EV", a[1], 0), ("EV", a[2], 0), ("EV", a[3], 0)]),
    ]
    emit("d4-merge-window",
         "F06 mergeA: same-address requests under one miss window, with controls.",
         phases)


def trace_wb_4state():
    """F12: four victim states in one set, dirty/clean x touched/untouched,
    then a full-set dirty replacement round so each state is evicted once;
    MR readback verifies the dirty payloads came back."""
    phases = []
    for s in range(0x130, 0x138):
        base = s * 0x40
        dirty = [base + n * L2_STRIDE for n in range(4)]     # A(touched) B(untouched)
        clean = [base + n * L2_STRIDE for n in range(4, 8)]  # C(touched) D(untouched)
        evict = [base + n * L2_STRIDE for n in range(8, 16)] # fresh dirty evictors
        phases += [
            (f"set 0x{s:x}: dirty fills (A touched n0/n1, B untouched n2/n3)",
             [("MW", a, 1) for a in dirty]),
            (f"set 0x{s:x}: clean fills (C touched n4/n5, D untouched n6/n7)",
             [("MR", a, 1) for a in clean]),
            (f"set 0x{s:x}: touch A and C only",
             [("MR", dirty[0], 1), ("MR", dirty[1], 1),
              ("MR", clean[0], 1), ("MR", clean[1], 1)] * 2),
            (f"set 0x{s:x}: 8 fresh dirty lines evict all four states",
             [("MW", a, 1) for a in evict]),
            (f"set 0x{s:x}: readback original 8 then the evictors",
             [("MR", a, 1) for a in dirty + clean + evict]),
        ]
    emit("d4-wb-4state",
         "F12 writeback four states: dirty/clean x touched/untouched victims, dirty eviction, readback.",
         phases)


def trace_snp_state_matrix():
    """F19: respCacheState corners. Three L2-resident holder groups
    (SC via LD, UD via MW, UC via CR+EV) stacked in their own L3 sets;
    clean filler released from L2 so L3 evictions prefer live targets."""
    groups = [("SC", 0x3, "LD"), ("UD", 0x7, "MW"), ("UC", 0xB, "CR")]
    phases = []
    for tag, s, op in groups:
        tgt = set_lines(s * 0x40, range(8))
        fill = set_lines(s * 0x40, range(8, 16))
        phases += [
            (f"group {tag}: 8 targets in L2/L3 set 0x{s:x}", [(op, a, 0 if op != "MW" else 1) for a in tgt]),
            (f"group {tag}: filler lines 8..15 fill the L3 set to 16 ways",
             [("LD", a, 0) for a in fill]),
            (f"group {tag}: drop the fillers from L2 (L3 keeps them)",
             [("EV", a, 0) for a in fill]),
        ]
    # UC targets: L1 must release its T copies so the L2 lines are UC
    uc = set_lines(0xB * 0x40, range(8))
    phases.append(("group UC: release L1 T copies -> L2 unique clean",
                   [("EV", a, 0) for a in uc]))
    for r in range(10):
        phases.append(
            (f"round {r}: refresh targets, then one fresh line per L3 set forces one eviction",
             [("LD", a, 0) for a in set_lines(0x3 * 0x40, range(8))]
             + [("MW", a, 1) for a in set_lines(0x7 * 0x40, range(8))]
             + ([("CR", a, 0) for a in uc] + [("EV", a, 0) for a in uc] if r % 2 == 0 else [])
             + [("LD", set_lines(b * 0x40, [16 + r])[0], 0) for _, b, _ in groups]))
    emit("d4-snp-state-matrix",
         "F19 snoop respCacheState corners: SC/UD/UC holders, one forced L3 eviction per round.",
         phases)


def trace_snp_dirty_mix():
    """F20 supplement: dirty x snoop-type. Four holder flavors
    (L2-UD, L1-T-dirty, L2-SC, released-UC) with mixed clean/dirty filler
    so L3 victims exercise both data-less and data-carrying snoops, plus
    an in-flight dirty release window before each eviction round."""
    groups = [
        ("UD-l2", 0x13, "mw"),      # matrix PutFull: dirty in L2, no client
        ("T-l1",  0x17, "crst"),    # CR then ST: L1 holds T dirty
        ("SC",    0x1B, "ld"),      # plain shared clean
        ("UC",    0x1F, "crev"),    # CR then EV: L2 unique clean
    ]
    phases = []
    for tag, s, kind in groups:
        tgt = set_lines(s * 0x40, range(8))
        fill = set_lines(s * 0x40, range(8, 16))
        if kind == "mw":
            ops = [("MW", a, 1) for a in tgt]
        elif kind == "crst":
            ops = [("CR", a, 0) for a in tgt] + [("ST", a, 0) for a in tgt]
        elif kind == "ld":
            ops = [("LD", a, 0) for a in tgt]
        else:
            ops = [("CR", a, 0) for a in tgt]
        phases.append((f"group {tag}: 8 targets", ops))
        if kind == "crev":
            phases.append((f"group {tag}: release L1 T copies",
                           [("EV", a, 0) for a in tgt]))
        # filler: dirty flavor for the two T-dirty groups, clean for the rest
        if kind in ("crst", "mw"):
            phases.append((f"group {tag}: dirty filler 8..15 (stays until churn)",
                           [("MW", a, 1) for a in fill]))
        else:
            phases.append((f"group {tag}: clean filler 8..15, dropped from L2",
                           [("LD", a, 0) for a in fill] + [("EV", a, 0) for a in fill]))
    for r in range(8):
        # in-flight dirty release window on a scratch set right before the
        # eviction lands: overlap nested writeback with the incoming snoop
        scratch = 0x150 * 0x40 + r * L2_STRIDE
        phases.append((f"round {r}: dirty release window on a scratch line",
                       [("CR", scratch, 0), ("ST", scratch, 0), ("EV", scratch, 0)]))
        refresh = []
        for tag, s, kind in groups:
            tgt = set_lines(s * 0x40, range(8))
            if kind == "mw":
                refresh += [("MW", a, 1) for a in tgt]
            elif kind == "crst":
                refresh += [("ST", a, 0) for a in tgt]
            elif kind == "ld":
                refresh += [("LD", a, 0) for a in tgt]
            else:
                refresh += [("CR", a, 0) for a in tgt] if r % 2 == 0 else []
        fresh = [("LD" if r % 2 == 0 else "MW", set_lines(s * 0x40, [16 + r])[0],
                  0 if r % 2 == 0 else 1) for _, s, _ in groups]
        phases.append((f"round {r}: refresh targets, then one fresh line per set", refresh + fresh))
    emit("d4-snp-dirty-mix",
         "F20 dirty x snoop-type mix: four holder flavors, mixed filler, nested-release overlap.",
         phases)


def trace_get_trunk_probe():
    """F03: Get hitting TRUNK with a client -> need_probe
    (MainPipe.scala:258-264). Phase 1 builds TRUNK+client via agent0
    acquires; phase 2 has agent1 (MAgent Get) read the same lines so the
    L2 must probe l1d; phase 3 re-reads (post-probe downgrade) and adds
    PutFull-on-TRUNK which takes the req_putfull branch of need_probe."""
    sets = [s * 0x40 for s in (0x11, 0x31, 0x51)]
    lines = [b + n * L2_STRIDE for b in sets for n in range(8)]
    phases = [
        ("phase 1: agent0 acquires 24 lines (3 sets x 8 ways) -> dir TRUNK",
         [("LD", a, 0) for a in lines]),
        ("phase 2: agent1 Get hits TRUNK with client -> SourceB probe l1d",
         [("MR", a, 1) for a in lines]),
        ("phase 3: agent0 re-reads set 1 (downgraded), agent1 PutFull on set 3",
         [("LD", a, 0) for a in lines[0:8]] +
         [("MW", a, 1) for a in lines[16:24]]),
    ]
    emit("d4-get-trunk-probe",
         "F03 Get/PutFull hitting TRUNK with client: need_probe -> SourceB probes l1d.",
         phases)


def main():
    TRACES.mkdir(exist_ok=True)
    trace_promote_burst()
    trace_put_miss_burst()
    trace_merge_window()
    trace_wb_4state()
    trace_snp_state_matrix()
    trace_snp_dirty_mix()
    trace_get_trunk_probe()


if __name__ == "__main__":
    main()
