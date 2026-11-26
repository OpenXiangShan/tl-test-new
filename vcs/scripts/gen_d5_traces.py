#!/usr/bin/env python3
"""Generate the d5-* directed traces (gap-analysis batch from
docs/l2-coverage-map.md section 4.1).

d5-share-matrix (theme 1, cross-agent sharing): every d4 trace groups
addresses per agent, so the L2 never probes between its clients. This
trace builds a state on l1d (agent 0), then lets the matrix client
(agent 1) read/write the same lines, driving SourceB probe params
(Consts.scala isToB/isParamFromT), the SourceB probe-queue entries
beyond index 0, SinkC task/data buffers and Directory state churn.

d5-cmo-matrix (theme 5, CBO formal matrix): CL hint over a state
lattice {L2-dirty, L2-clean, client-dirty, client-clean, miss, shared}
with readback; each hint is closed by the CBOAck backpressure (a
following access on the same line waits out the in-flight CMO).
FL/IV cells are generated only with --emit-fliv: the tl2tl CBO
meta-pollution bug is unresolved upstream, running them risks
directory corruption.

Geometry: L2 512 sets x 8 ways, 64B block; set = addr[14:6].
Groups sit 0x4000 apart (256 sets), 4 lines each -> <= 4 ways per set.

Agents (tl_ul = 0 configs): 0 = CAgent(l1d), 1 = MAgent(matrix).
Ops: LD/CR/ST/EV on agent 0 (acquire S / acquire T / acquirePerm /
releaseData), MR/MW/CR on agent 1 (Get / PutFullData / cached Get),
CL = CBO clean hint (agent 0 only, alwaysHit=false), FE = fence.
"""

import sys
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


def group_lines(group, count=4):
    base = 0x100000 + group * 0x4000
    return [base + n * 0x40 for n in range(count)]


def trace_share_matrix():
    """Cross-agent lattice: build on l1d, then matrix reads/writes the
    same lines. Groups g1..g10, 4 lines each, distinct sets."""
    g = [group_lines(i) for i in range(11)]
    phases = [
        ("g1 p1: l1d takes dirty T (CR then ST)", [("CR", a, 0) for a in g[0]] +
         [("ST", a, 0) for a in g[0]]),
        ("g1 p2: matrix reads -> probe/downgrade l1d",
         [("MR", a, 1) for a in g[0]]),
        ("g1 p3: l1d re-reads (state after downgrade)",
         [("LD", a, 0) for a in g[0]]),
        ("g1 p4: matrix re-reads (shared steady state)",
         [("MR", a, 1) for a in g[0]]),

        ("g2 p1: l1d takes dirty T", [("CR", a, 0) for a in g[1]] +
         [("ST", a, 0) for a in g[1]]),
        ("g2 p2: matrix full write -> probe must invalidate l1d",
         [("MW", a, 1) for a in g[1]]),
        ("g2 p3: l1d re-reads (must refetch matrix data)",
         [("LD", a, 0) for a in g[1]]),

        ("g3 p1: l1d takes clean T via acquirePerm (ST, no data)",
         [("ST", a, 0) for a in g[2]]),
        ("g3 p2: matrix reads -> downgrade probe on clean T",
         [("MR", a, 1) for a in g[2]]),
        ("g3 p3: l1d re-reads", [("LD", a, 0) for a in g[2]]),

        ("g4 p1: l1d takes clean T via acquirePerm",
         [("ST", a, 0) for a in g[3]]),
        ("g4 p2: matrix full write on clean T",
         [("MW", a, 1) for a in g[3]]),
        ("g4 p3: l1d re-reads", [("LD", a, 0) for a in g[3]]),

        ("g5 p1: l1d reads (B/T)", [("LD", a, 0) for a in g[4]]),
        ("g5 p2: matrix reads -> shared",
         [("MR", a, 1) for a in g[4]]),
        ("g5 p3: l1d promotes (CR = BtoT upgrade)",
         [("CR", a, 0) for a in g[4]]),
        ("g5 p4: l1d releases (EV = releaseData)",
         [("EV", a, 0) for a in g[4]]),
        ("g5 p5: matrix re-reads (line back to L2-only)",
         [("MR", a, 1) for a in g[4]]),

        ("g6 p1: l1d reads", [("LD", a, 0) for a in g[5]]),
        ("g6 p2: matrix reads -> shared B holder",
         [("MR", a, 1) for a in g[5]]),
        ("g6 p3: matrix full write -> probe B holder to N",
         [("MW", a, 1) for a in g[5]]),
        ("g6 p4: l1d re-reads (must refetch)",
         [("LD", a, 0) for a in g[5]]),

        ("g7 p1: l1d takes dirty T", [("CR", a, 0) for a in g[6]] +
         [("ST", a, 0) for a in g[6]]),
        ("g7 p2: matrix cached read (CR) of dirty line",
         [("CR", a, 1) for a in g[6]]),
        ("g7 p3: l1d re-reads", [("LD", a, 0) for a in g[6]]),

        ("g8 p1: l1d reads", [("LD", a, 0) for a in g[7]]),
        ("g8 p2: matrix reads -> shared",
         [("MR", a, 1) for a in g[7]]),
        ("g8 p3: l1d voluntary release of shared line",
         [("EV", a, 0) for a in g[7]]),
        ("g8 p4: matrix re-reads", [("MR", a, 1) for a in g[7]]),

        ("g9 p1: matrix full write -> L2 dirty, no client",
         [("MW", a, 1) for a in g[8]]),
        ("g9 p2: l1d reads dirty line (writeback-on-read path)",
         [("LD", a, 0) for a in g[8]]),
        ("g9 p3: matrix re-reads", [("MR", a, 1) for a in g[8]]),

        ("g10 p1: l1d reads", [("LD", a, 0) for a in g[9]]),
        ("g10 p2: l1d promotes to dirty T",
         [("CR", a, 0) for a in g[9]] + [("ST", a, 0) for a in g[9]]),
        ("g10 p3: matrix reads (probe dirty holder)",
         [("MR", a, 1) for a in g[9]]),
        ("g10 p4: l1d releases data",
         [("EV", a, 0) for a in g[9]]),
        ("g10 p5: matrix re-reads",
         [("MR", a, 1) for a in g[9]]),

        # ReleaseData chain: acquirePerm only fires from a downgraded state
        # (on a held T line CR/ST are PERMISSION no-ops and never mark the
        # acquirePerm board), and do_releaseDataAuto sends data only when
        # dirty || acquirePermPrev.  LD -> matrix read (downgrade to S) ->
        # ST (real acquirePerm) -> EV (ReleaseData -> SinkC dataBuf).
        ("g11 p1: l1d reads", [("LD", a, 0) for a in g[10]]),
        ("g11 p2: matrix reads (downgrade l1d to S)",
         [("MR", a, 1) for a in g[10]]),
        ("g11 p3: l1d acquirePerm from S (real upgrade, marks board)",
         [("ST", a, 0) for a in g[10]]),
        ("g11 p4: l1d releases with data (ReleaseData)",
         [("EV", a, 0) for a in g[10]]),
        ("g11 p5: matrix re-reads (line back to L2, writeback happened)",
         [("MR", a, 1) for a in g[10]]),
    ]
    emit("d5-share-matrix",
         "d5 cross-agent share matrix: l1d state x matrix read/write, "
         "10 groups x 4 lines (F-share: SourceB/SinkC/Directory/Consts).",
         phases)


def cmo_phases(op, tag):
    """One CL/FL/IV op over the state lattice; readback after each hint
    closes on the CBOAck (same-address ops wait out the in-flight CMO)."""
    c = [group_lines(16 + i) for i in range(6)]  # region above share groups
    hint = op.upper()
    return [
        (f"{tag} c1 p1: matrix full write -> L2 dirty, no client",
         [("MW", a, 1) for a in c[0]]),
        (f"{tag} c1 p2: {hint} on L2-dirty line",
         [(hint, a, 0) for a in c[0]]),
        (f"{tag} c1 p3: matrix readback", [("MR", a, 1) for a in c[0]]),

        (f"{tag} c2 p1: matrix read -> L2 clean, no client",
         [("MR", a, 1) for a in c[1]]),
        (f"{tag} c2 p2: {hint} on L2-clean line",
         [(hint, a, 0) for a in c[1]]),
        (f"{tag} c2 p3: matrix readback", [("MR", a, 1) for a in c[1]]),

        (f"{tag} c3 p1: l1d takes dirty T", [("CR", a, 0) for a in c[2]] +
         [("ST", a, 0) for a in c[2]]),
        (f"{tag} c3 p2: {hint} on client-dirty line (clean through client)",
         [(hint, a, 0) for a in c[2]]),
        (f"{tag} c3 p3: l1d readback", [("LD", a, 0) for a in c[2]]),

        (f"{tag} c4 p1: l1d reads (client clean state)",
         [("LD", a, 0) for a in c[3]]),
        (f"{tag} c4 p2: {hint} on client-clean line",
         [(hint, a, 0) for a in c[3]]),
        (f"{tag} c4 p3: l1d readback", [("LD", a, 0) for a in c[3]]),

        (f"{tag} c5 p1: {hint} on cold line (miss)",
         [(hint, a, 0) for a in c[4]]),
        (f"{tag} c5 p2: matrix touches the line afterwards",
         [("MR", a, 1) for a in c[4]]),

        (f"{tag} c6 p1: l1d reads", [("LD", a, 0) for a in c[5]]),
        (f"{tag} c6 p2: matrix reads -> shared",
         [("MR", a, 1) for a in c[5]]),
        (f"{tag} c6 p3: {hint} on shared line",
         [(hint, a, 0) for a in c[5]]),
        (f"{tag} c6 p4: l1d readback", [("LD", a, 0) for a in c[5]]),
    ]


def trace_cmo_matrix(emit_fliv=False):
    """CL over the 6-state lattice (12 cells with the miss/shared splits).
    FL/IV lattices are only written with --emit-fliv (upstream CBO
    meta-pollution bug unresolved; running them risks dir corruption)."""
    emit("d5-cmo-matrix",
         "d5 CBO clean-hint matrix: CL x {L2-dirty, L2-clean, client-dirty,"
         " client-clean, miss, shared}, 4 lines each, readback closes on"
         " CBOAck (F28/F29).",
         cmo_phases("CL", "CL"))
    if emit_fliv:
        for op in ("FL", "IV"):
            emit(f"d5-cmo-matrix-{op.lower()}",
                 f"d5 CBO {op} matrix: same 6-state lattice. Run only after"
                 " the upstream CBO meta-pollution bug is cleared.",
                 cmo_phases(op, op))


def trace_mmio_matrix():
    """MMIO depth/contention matrix (gap theme 2).  The suite's mmio jobs
    predate commit 724c625 (16 source IDs): they ran with 2 sources, so at
    most 2 MMIOBridge entries were ever allocated.  Back-to-back MG with
    16 sources (idpool == mmioBridgeSize) fills entries 0-15; same-address
    read pairs drive the read-receipt block (MMIOBridge.scala:396-402);
    write-then-read and MG/MP interleave cover the txdat/DBIDResp path.
    Needs an mmio-enabled ini (region 0x6000000-0x7000000)."""
    base = 0x6000000
    step = 0x1000
    w1  = [base + 0 * step + n * 8 for n in range(16)]
    r1  = [base + 1 * step + n * 8 for n in range(16)]
    rb  = w1                                   # readback of the writes
    pr  = [base + 2 * step + n * 8 for n in range(4)]
    mix = [base + 3 * step + n * 8 for n in range(16)]
    ser = base + 4 * step
    wr2 = [base + 5 * step + n * 8 for n in range(16)]
    phases = [
        ("m1: 16 back-to-back MP (txdat + DBIDResp path)",
         [("MP", a, 0) for a in w1]),
        ("m2: 16 back-to-back MG (fill MMIOBridge entries 0-15)",
         [("MG", a, 0) for a in r1]),
        ("m3: MG readback of m1 writes",
         [("MG", a, 0) for a in rb]),
        ("m4: same-address MG pairs (read-receipt block, :396-402)",
         [("MG", a, 0) for a in pr for _ in range(2)]),
        ("m5: MG/MP interleave (txreq arb contention)",
         [("MG" if n % 2 == 0 else "MP", a, 0) for n, a in enumerate(mix)]),
        ("m6: four MG on one address (serialization)",
         [("MG", ser, 0)] * 4),
        ("m7: 16 MP then 16 MG on the same lines",
         [("MP", a, 0) for a in wr2] + [("MG", a, 0) for a in wr2]),
    ]
    emit("d5-mmio-matrix",
         "d5 MMIO matrix: depth-16 bursts, same-addr read pairs, MG/MP "
         "interleave, write-then-read (theme 2: MMIOBridge entries/receipt).",
         phases)


def main():
    trace_share_matrix()
    trace_cmo_matrix(emit_fliv="--emit-fliv" in sys.argv)
    trace_mmio_matrix()


if __name__ == "__main__":
    main()
