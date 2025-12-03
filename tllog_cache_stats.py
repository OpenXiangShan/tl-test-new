#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import bisect
import collections
import json
import os
import re
import sqlite3
import statistics
from dataclasses import dataclass
from typing import Deque, Dict, Iterable, List, Optional, Tuple, Any


# -----------------------
# Data model
# -----------------------

@dataclass(frozen=True)
class Event:
    cycle: int
    agent: str
    chan: str
    op: str
    param: str
    sink: int
    source: int
    addr: int
    data: int
    user: int
    echo: int


# -----------------------
# Parsing converted text
# -----------------------

USER_ECHO_RE = re.compile(r"user:\s*([0-9a-fA-F]+)\s+echo:\s*([0-9a-fA-F]+)")

def parse_converted_line(line: str) -> Optional[Event]:
    line = line.strip()
    if not line or "user:" not in line:
        return None

    m = USER_ECHO_RE.search(line)
    if not m:
        return None
    user = int(m.group(1), 16)
    echo = int(m.group(2), 16)

    head = line[:m.start()].strip()
    toks = head.split()
    if len(toks) < 8:
        return None

    try:
        cycle = int(toks[0])
        agent = toks[1]
        chan = toks[2]
        op = toks[3]
        sink = int(toks[-4])
        source = int(toks[-3])
        addr = int(toks[-2], 16)
        data = int(toks[-1], 16)
        param = " ".join(toks[4:-4])
    except Exception:
        return None

    return Event(
        cycle=cycle,
        agent=agent,
        chan=chan,
        op=op,
        param=param,
        sink=sink,
        source=source,
        addr=addr,
        data=data,
        user=user,
        echo=echo,
    )


def iter_events_from_text(path: str) -> Iterable[Event]:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            ev = parse_converted_line(line)
            if ev is not None:
                yield ev


# -----------------------
# DB reading (best-effort)
# -----------------------

def _pick_col(cols: List[str], *candidates: str) -> Optional[str]:
    lower = {c.lower(): c for c in cols}
    for cand in candidates:
        if cand.lower() in lower:
            return lower[cand.lower()]
    return None


def iter_events_from_db(db_path: str, table: str = "TLLog") -> Iterable[Event]:
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    cur.execute(f"PRAGMA table_info({table})")
    info = cur.fetchall()
    if not info:
        raise RuntimeError(f"Table {table} not found in {db_path}")
    cols = [row["name"] for row in info]

    raw_col = _pick_col(cols, "log", "raw", "msg", "message", "text", "line")
    has_struct = all(_pick_col(cols, x) is not None for x in ("cycle", "agent", "chan", "op"))

    if raw_col and not has_struct:
        cur.execute(f"SELECT {raw_col} AS raw FROM {table}")
        for row in cur:
            ev = parse_converted_line(str(row["raw"]))
            if ev is not None:
                yield ev
        conn.close()
        return

    c_cycle = _pick_col(cols, "cycle", "time", "t", "timestamp")
    c_agent = _pick_col(cols, "agent", "node", "port", "interface", "bundle")
    c_chan  = _pick_col(cols, "chan", "channel")
    c_op    = _pick_col(cols, "op", "opcode", "message_type")
    c_param = _pick_col(cols, "param", "params")
    c_sink  = _pick_col(cols, "sink")
    c_src   = _pick_col(cols, "source", "src")
    c_addr  = _pick_col(cols, "address", "addr")
    c_data  = _pick_col(cols, "data")
    c_user  = _pick_col(cols, "user")
    c_echo  = _pick_col(cols, "echo")

    needed = [c_cycle, c_agent, c_chan, c_op, c_sink, c_src, c_addr, c_data]
    if any(x is None for x in needed):
        cur.execute(f"SELECT * FROM {table}")
        for row in cur:
            joined = " ".join(str(row[c]) for c in cols if row[c] is not None)
            ev = parse_converted_line(joined)
            if ev is not None:
                yield ev
        conn.close()
        return

    sel = {
        "cycle": c_cycle,
        "agent": c_agent,
        "chan": c_chan,
        "op": c_op,
        "param": c_param,
        "sink": c_sink,
        "source": c_src,
        "addr": c_addr,
        "data": c_data,
        "user": c_user,
        "echo": c_echo,
    }

    select_exprs = []
    for k, c in sel.items():
        if c is None:
            if k == "param":
                select_exprs.append("'' AS param")
            else:
                select_exprs.append(f"0 AS {k}")
        else:
            select_exprs.append(f"{c} AS {k}")

    sql = f"SELECT {', '.join(select_exprs)} FROM {table}"
    cur.execute(sql)

    for row in cur:
        try:
            cycle = int(row["cycle"])
            agent = str(row["agent"])
            chan = str(row["chan"])
            op = str(row["op"])
            param = str(row["param"])
            sink = int(row["sink"])
            source = int(row["source"])

            addr_raw = row["addr"]
            data_raw = row["data"]

            if isinstance(addr_raw, str):
                addr_raw = addr_raw.strip()
                addr = int(addr_raw, 16) if addr_raw.startswith(("0x",)) else int(addr_raw)
            else:
                addr = int(addr_raw)

            if isinstance(data_raw, str):
                data_raw = data_raw.strip()
                data = int(data_raw, 16) if data_raw.startswith(("0x",)) else int(data_raw)
            else:
                data = int(data_raw)

            user = int(row["user"]) if "user" in row.keys() else 0
            echo = int(row["echo"]) if "echo" in row.keys() else 0
        except Exception:
            continue

        yield Event(
            cycle=cycle, agent=agent, chan=chan, op=op, param=param,
            sink=sink, source=source, addr=addr, data=data, user=user, echo=echo
        )

    conn.close()


# -----------------------
# Helpers
# -----------------------

def matrix_id(agent: str) -> str:
    m = re.search(r"\[(\d+)\]", agent)
    return m.group(1) if m else "?"

def percentile_int(xs: List[int], p: float) -> int:
    if not xs:
        return 0
    xs_sorted = sorted(xs)
    k = (len(xs_sorted) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs_sorted) - 1)
    if f == c:
        return xs_sorted[f]
    return int(xs_sorted[f] + (xs_sorted[c] - xs_sorted[f]) * (k - f))

def stats_int(xs: List[int]) -> Dict[str, Any]:
    if not xs:
        return {}
    return {
        "count": len(xs),
        "avg": float(statistics.mean(xs)),
        "p50": int(statistics.median(xs)),
        "p90": int(percentile_int(xs, 90)),
        "p99": int(percentile_int(xs, 99)),
        "max": int(max(xs)),
        "min": int(min(xs)),
    }

def fmt_rate(x: Optional[float]) -> str:
    if x is None:
        return "N/A"
    return f"{x * 100:.2f}%"

def infer_mask_from_max_source(max_src: int) -> int:
    width = max(1, int(max_src).bit_length())
    return (1 << width) - 1


# -----------------------
# Core analysis
# -----------------------

def analyze(
    events: Iterable[Event],
    matrix_prefix: str = "L2_Matrix",
    upstream_agents: Tuple[str, ...] = ("L3_L2", "MEM_L3"),
    upstream_a_ops: Tuple[str, ...] = ("Get", "AcquireBlock"),
    resp_ops: Tuple[Tuple[str, str], ...] = (("M", "MatrixData"), ("D", "AccessAckData")),
    mshr_scope: str = "cache",  # cache | agent | matrix
    debug_unmatched: int = 0,
) -> Dict[str, Any]:
    # 1) collect
    upstream_by_addr: Dict[int, List[int]] = collections.defaultdict(list)
    reqs: List[Event] = []
    resps: List[Event] = []
    max_req_src_by_agent: Dict[str, int] = {}

    for ev in events:
        if ev.agent in upstream_agents and ev.chan == "A" and ev.op in upstream_a_ops:
            upstream_by_addr[ev.addr].append(ev.cycle)

        if not ev.agent.startswith(matrix_prefix):
            continue

        if ev.chan == "A" and ev.op == "Get":
            reqs.append(ev)
            mx = max_req_src_by_agent.get(ev.agent, -1)
            if ev.source > mx:
                max_req_src_by_agent[ev.agent] = ev.source
            continue

        for (rchan, rop) in resp_ops:
            if ev.chan == rchan and ev.op == rop:
                resps.append(ev)
                break

    reqs.sort(key=lambda x: x.cycle)
    resps.sort(key=lambda x: x.cycle)
    for addr, ts in upstream_by_addr.items():
        ts.sort()

    # 2) infer source mask per agent
    mask_by_agent: Dict[str, int] = {
        agent: infer_mask_from_max_source(mx)
        for agent, mx in max_req_src_by_agent.items()
    }
    def mask_of(agent: str, src_val: int) -> int:
        return mask_by_agent.get(agent, infer_mask_from_max_source(src_val))

    # 3) match req->resp using (agent, addr, source_low)
    # store resp queue elements: (cycle, chan, op, source, sink)
    resp_cycles_by_key: Dict[Tuple[str, int, int], List[Tuple[int, str, str, int, int]]] = collections.defaultdict(list)
    for ev in resps:
        m = mask_of(ev.agent, ev.source)
        key = (ev.agent, ev.addr, ev.source & m)
        resp_cycles_by_key[key].append((ev.cycle, ev.chan, ev.op, ev.source, ev.sink))

    resp_q: Dict[Tuple[str, int, int], Deque[Tuple[int, str, str, int, int]]] = {}
    for key, lst in resp_cycles_by_key.items():
        lst.sort(key=lambda x: x[0])
        resp_q[key] = collections.deque(lst)

    matched: List[Dict[str, Any]] = []
    unmatched_reqs: List[Event] = []

    for rq in reqs:
        m = mask_of(rq.agent, rq.source)
        key = (rq.agent, rq.addr, rq.source & m)
        q = resp_q.get(key)
        if not q:
            unmatched_reqs.append(rq)
            continue
        while q and q[0][0] < rq.cycle:
            q.popleft()
        if not q:
            unmatched_reqs.append(rq)
            continue

        (rcycle, rchan, rop, rsrc, rsink) = q.popleft()
        matched.append({
            "req": rq,
            "resp_cycle": rcycle,
            "resp_chan": rchan,
            "resp_op": rop,
            "resp_source": rsrc,
            "resp_sink": rsink,
        })

    # 4) upstream-miss evidence (primary miss detector)
    #    upstream_miss := any upstream A(Get/AcquireBlock) to same addr in [req_cycle, resp_cycle]
    upstream_miss_flag: List[bool] = []
    for item in matched:
        rq: Event = item["req"]
        rcycle: int = item["resp_cycle"]
        times = upstream_by_addr.get(rq.addr, [])
        i = bisect.bisect_left(times, rq.cycle)
        upstream_miss = (i < len(times) and times[i] <= rcycle)
        upstream_miss_flag.append(upstream_miss)

    # 5) classify into {miss, mshr_merge, tag_hit}
    #    mshr_merge if at req time there's an outstanding PRIMARY MISS for same addr (scope adjustable)
    def scope_key(req: Event) -> Tuple:
        if mshr_scope == "cache":
            return (req.addr,)
        if mshr_scope == "agent":
            return (req.agent, req.addr)
        if mshr_scope == "matrix":
            return (matrix_id(req.agent), req.addr)
        raise ValueError("mshr_scope must be one of: cache, agent, matrix")

    # outstanding primary miss intervals: key -> list of resp_cycles (min-heap-like via sorted list)
    outstanding: Dict[Tuple, List[int]] = collections.defaultdict(list)

    def prune(key: Tuple, now_cycle: int):
        lst = outstanding.get(key)
        if not lst:
            return
        # remove all completed (resp_cycle <= now)
        # list is kept sorted, so pop from front
        j = 0
        while j < len(lst) and lst[j] <= now_cycle:
            j += 1
        if j > 0:
            del lst[:j]
        if not lst:
            outstanding.pop(key, None)

    annotated: List[Dict[str, Any]] = []
    counts = {"miss": 0, "mshr_merge": 0, "tag_hit": 0}

    for idx, item in enumerate(matched):
        rq: Event = item["req"]
        rcycle: int = item["resp_cycle"]
        key = scope_key(rq)
        prune(key, rq.cycle)

        upstream_miss = upstream_miss_flag[idx]
        is_merge = (len(outstanding.get(key, [])) > 0)  # outstanding primary miss exists
        if is_merge:
            cls = "mshr_merge"
        else:
            cls = "miss" if upstream_miss else "tag_hit"

        # If it's a primary miss, open an outstanding interval until its response
        if cls == "miss":
            lst = outstanding[key]
            # keep sorted
            bisect.insort(lst, rcycle)

        counts[cls] += 1

        annotated.append({
            "idx": idx,
            "matrix": matrix_id(rq.agent),
            "agent": rq.agent,
            "cycle_req": rq.cycle,
            "cycle_resp": rcycle,
            "latency": rcycle - rq.cycle,
            "source_req": rq.source,
            "addr": rq.addr,
            "addr_hex": f"0x{rq.addr:x}",
            "class": cls,                 # miss / mshr_merge / tag_hit
            "hit_any": (cls != "miss"),   # old-style "hit"
            "resp_kind": f"{item['resp_chan']} {item['resp_op']}",
        })

    # 6) overall hit-rates
    total = len(annotated)
    miss_n = counts["miss"]
    tag_n = counts["tag_hit"]
    merge_n = counts["mshr_merge"]
    hit_any_n = tag_n + merge_n

    latencies = [x["latency"] for x in annotated]
    lat_stat = stats_int(latencies)

    # 7) access-count histogram + per-address lists
    by_addr: Dict[int, List[Dict[str, Any]]] = collections.defaultdict(list)
    for x in annotated:
        by_addr[x["addr"]].append(x)

    access_hist = collections.Counter(len(v) for v in by_addr.values())
    access_hist = {str(k): int(v) for k, v in sorted(access_hist.items(), key=lambda kv: int(kv[0]))}

    # 8) reuse stats (2nd+)
    reuse_total = 0
    reuse_tag = 0
    reuse_merge = 0
    reuse_any = 0

    # reuse distance
    reuse_dist_intervening: List[int] = []
    reuse_dist_cycles: List[int] = []

    # store last access position per addr
    last_idx_by_addr: Dict[int, int] = {}
    last_cycle_by_addr: Dict[int, int] = {}

    for x in annotated:
        addr = x["addr"]
        if addr in last_idx_by_addr:
            reuse_total += 1
            if x["class"] == "tag_hit":
                reuse_tag += 1
            elif x["class"] == "mshr_merge":
                reuse_merge += 1
            if x["class"] != "miss":
                reuse_any += 1

            reuse_dist_intervening.append(x["idx"] - last_idx_by_addr[addr] - 1)
            reuse_dist_cycles.append(x["cycle_req"] - last_cycle_by_addr[addr])

        last_idx_by_addr[addr] = x["idx"]
        last_cycle_by_addr[addr] = x["cycle_req"]

    reuse_dist_stats = {
        "intervening_reads": stats_int(reuse_dist_intervening),
        "cycle_gap": stats_int(reuse_dist_cycles),
    }

    # 9) per-matrix breakdown
    per_matrix: Dict[str, Dict[str, Any]] = {}
    by_addr_matrix: Dict[Tuple[str, int], List[Dict[str, Any]]] = collections.defaultdict(list)

    for x in annotated:
        mid = x["matrix"]
        per_matrix.setdefault(mid, {
            "total": 0,
            "miss": 0,
            "tag_hit": 0,
            "mshr_merge": 0,
            "hit_any": 0,
            "reuse_total": 0,
            "reuse_tag_hit": 0,
            "reuse_mshr_merge": 0,
            "reuse_hit_any": 0,
        })
        per_matrix[mid]["total"] += 1
        per_matrix[mid][x["class"]] += 1
        per_matrix[mid]["hit_any"] += 1 if x["class"] != "miss" else 0
        by_addr_matrix[(mid, x["addr"])].append(x)

    for (mid, addr), lst in by_addr_matrix.items():
        lst.sort(key=lambda z: z["idx"])
        if len(lst) <= 1:
            continue
        for x in lst[1:]:
            per_matrix[mid]["reuse_total"] += 1
            if x["class"] == "tag_hit":
                per_matrix[mid]["reuse_tag_hit"] += 1
            elif x["class"] == "mshr_merge":
                per_matrix[mid]["reuse_mshr_merge"] += 1
            if x["class"] != "miss":
                per_matrix[mid]["reuse_hit_any"] += 1

    for mid, d in per_matrix.items():
        d["hit_rate_any"] = (d["hit_any"] / d["total"]) if d["total"] else None
        d["tag_hit_rate"] = (d["tag_hit"] / d["total"]) if d["total"] else None
        d["mshr_merge_rate"] = (d["mshr_merge"] / d["total"]) if d["total"] else None
        d["reuse_hit_rate_any"] = (d["reuse_hit_any"] / d["reuse_total"]) if d["reuse_total"] else None
        d["reuse_tag_hit_rate"] = (d["reuse_tag_hit"] / d["reuse_total"]) if d["reuse_total"] else None
        d["reuse_mshr_merge_rate"] = (d["reuse_mshr_merge"] / d["reuse_total"]) if d["reuse_total"] else None

    # 10) debug unmatched
    debug_list = []
    if debug_unmatched > 0 and unmatched_reqs:
        for rq in unmatched_reqs[:debug_unmatched]:
            m = mask_of(rq.agent, rq.source)
            debug_list.append({
                "cycle": rq.cycle,
                "agent": rq.agent,
                "source_req": rq.source,
                "source_low": rq.source & m,
                "mask": hex(m),
                "addr_hex": f"0x{rq.addr:x}",
                "note": "no matched response found",
            })

    # 11) return json
    return {
        "assumptions": {
            "primary_miss_definition": "MISS if upstream (L3_L2/MEM_L3) A(Get/AcquireBlock) to same addr occurs between req..resp",
            "mshr_merge_definition": f"MSHR_MERGE if an outstanding PRIMARY MISS exists for same addr under scope='{mshr_scope}' at req time",
            "tag_hit_definition": "TAG_HIT if not MISS and not MSHR_MERGE",
            "reuse_definition": "per-address: ignore first access; compute rates for accesses #2..end; ignore single-touch addrs",
            "matrix_read_req": f"{matrix_prefix}* A Get",
            "matrix_read_resp": [f"{c} {o}" for (c, o) in resp_ops],
            "upstream_agents": list(upstream_agents),
            "upstream_a_ops": list(upstream_a_ops),
            "source_matching": "match by (agent, addr, source_low) where source_low = source & inferred_mask; mask inferred from request max source per agent",
        },
        "overall": {
            "total_reads": total,
            "miss": miss_n,
            "tag_hit": tag_n,
            "mshr_merge": merge_n,
            "hit_any": hit_any_n,
            "hit_rate_any": (hit_any_n / total) if total else None,
            "tag_hit_rate": (tag_n / total) if total else None,
            "mshr_merge_rate": (merge_n / total) if total else None,

            "reuse_total_reads": reuse_total,
            "reuse_tag_hits": reuse_tag,
            "reuse_mshr_merges": reuse_merge,
            "reuse_hits_any": reuse_any,
            "reuse_hit_rate_any": (reuse_any / reuse_total) if reuse_total else None,
            "reuse_tag_hit_rate": (reuse_tag / reuse_total) if reuse_total else None,
            "reuse_mshr_merge_rate": (reuse_merge / reuse_total) if reuse_total else None,

            "unmatched_reads": len(unmatched_reqs),
            "latency_cycles": lat_stat,
            "reuse_distance": reuse_dist_stats,
            "access_count_histogram": access_hist,
            "mask_by_agent": {k: hex(v) for k, v in mask_by_agent.items()},
            "mshr_scope": mshr_scope,
        },
        "per_matrix": per_matrix,
        "debug_unmatched": debug_list,
    }


# -----------------------
# CLI
# -----------------------

def main():
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--db", help="Path to chiseldb sqlite file, e.g. run/chiseldb.db")
    src.add_argument("--text", help="Path to converted TLLog text (output of convert_tllog.sh)")
    ap.add_argument("--table", default="TLLog", help="SQLite table name (default: TLLog)")
    ap.add_argument("--matrix-prefix", default="L2_Matrix", help="Matrix agent prefix (default: L2_Matrix)")
    ap.add_argument("--upstream-agents", default="L3_L2,MEM_L3", help="Comma-separated upstream agents (default: L3_L2,MEM_L3)")
    ap.add_argument("--upstream-a-ops", default="Get,AcquireBlock", help="Comma-separated upstream A ops treated as miss evidence (default: Get,AcquireBlock)")
    ap.add_argument("--mshr-scope", default="cache", choices=["cache", "agent", "matrix"],
                    help="Scope for outstanding miss detection: cache=across all matrices; agent=per agent; matrix=per Matrix[i]")
    ap.add_argument("--debug-unmatched", type=int, default=0, help="Include first N unmatched requests in JSON (default: 0)")
    ap.add_argument("--out-json", default="", help="Write summary json to this path (optional)")
    args = ap.parse_args()

    upstream_agents = tuple(s.strip() for s in args.upstream_agents.split(",") if s.strip())
    upstream_a_ops = tuple(s.strip() for s in args.upstream_a_ops.split(",") if s.strip())

    if args.db:
        if not os.path.exists(args.db):
            raise SystemExit(f"DB not found: {args.db}")
        events = iter_events_from_db(args.db, table=args.table)
    else:
        if not os.path.exists(args.text):
            raise SystemExit(f"Text log not found: {args.text}")
        events = iter_events_from_text(args.text)

    summary = analyze(
        events,
        matrix_prefix=args.matrix_prefix,
        upstream_agents=upstream_agents,
        upstream_a_ops=upstream_a_ops,
        mshr_scope=args.mshr_scope,
        debug_unmatched=args.debug_unmatched,
    )

    ov = summary["overall"]
    print("============================================================")
    print("TLLog Cache Stats (Get-based) — v2 w/ tag_hit vs mshr_merge")
    print("============================================================")
    print(f"Source masks per agent      : {ov['mask_by_agent']}")
    print(f"MSHR scope                  : {ov['mshr_scope']}")
    print("")
    print(f"Total matrix reads          : {ov['total_reads']}")
    print(f"  miss                      : {ov['miss']}")
    print(f"  tag_hit                   : {ov['tag_hit']}")
    print(f"  mshr_merge                : {ov['mshr_merge']}")
    print(f"Hit rate (any hit)          : {fmt_rate(ov['hit_rate_any'])}  (tag_hit+mshr_merge)")
    print(f"  tag_hit_rate              : {fmt_rate(ov['tag_hit_rate'])}")
    print(f"  mshr_merge_rate           : {fmt_rate(ov['mshr_merge_rate'])}")
    print("")
    print(f"Reuse reads (2nd+)           : {ov['reuse_total_reads']}")
    print(f"Reuse hit rate (any hit)     : {fmt_rate(ov['reuse_hit_rate_any'])}")
    print(f"Reuse tag-hit rate (hard)    : {fmt_rate(ov['reuse_tag_hit_rate'])}")
    print(f"Reuse mshr-merge rate         : {fmt_rate(ov['reuse_mshr_merge_rate'])}")
    print("")
    print(f"Unmatched reads (no resp)    : {ov['unmatched_reads']}")
    if ov.get("latency_cycles"):
        ls = ov["latency_cycles"]
        if ls:
            print("")
            print("Latency (cycles)            : "
                  f"avg={ls['avg']:.2f}, p50={ls['p50']}, p90={ls['p90']}, p99={ls['p99']}, max={ls['max']}")
    rd = ov.get("reuse_distance", {})
    if rd:
        print("")
        if rd.get("intervening_reads"):
            s = rd["intervening_reads"]
            if s:
                print("Reuse distance (intervening reads): "
                      f"avg={s['avg']:.2f}, p50={s['p50']}, p90={s['p90']}, p99={s['p99']}, max={s['max']}")
        if rd.get("cycle_gap"):
            s = rd["cycle_gap"]
            if s:
                print("Reuse distance (cycle gap)        : "
                      f"avg={s['avg']:.2f}, p50={s['p50']}, p90={s['p90']}, p99={s['p99']}, max={s['max']}")

    print("")
    print("Access-count histogram (#reads per unique addr -> #addrs):")
    for k, v in ov["access_count_histogram"].items():
        print(f"  {k:>4} -> {v}")

    print("")
    print("Per L2_Matrix[*] breakdown:")
    for mid in sorted(summary["per_matrix"].keys(), key=lambda x: (x == "?", int(x) if x.isdigit() else 1_000_000)):
        d = summary["per_matrix"][mid]
        print(f"  - Matrix[{mid}] : total={d['total']}, "
              f"hit_any={fmt_rate(d['hit_rate_any'])}, tag_hit={fmt_rate(d['tag_hit_rate'])}, merge={fmt_rate(d['mshr_merge_rate'])}, "
              f"reuse_any={fmt_rate(d['reuse_hit_rate_any'])}, reuse_tag={fmt_rate(d['reuse_tag_hit_rate'])}")

    if args.out_json:
        with open(args.out_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print("")
        print(f"Wrote summary JSON to: {args.out_json}")


if __name__ == "__main__":
    main()
