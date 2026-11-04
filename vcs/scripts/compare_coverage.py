#!/usr/bin/env python3
"""Compare two urg reports (e.g. single-trace baseline vs merged) side by side.

Usage:
  compare_coverage.py <report_dir_A> <report_dir_B> [--filter 'regex' ...]

Reads session.xml for the four overall metrics and modlist.html for the
per-module tree; prints a before/after table. --filter takes python regexes
matched against the flattened instance path (e.g. 'l3.*sinkB'); default is a
curated set of the coverage-gap modules.
"""
import argparse
import os
import re
import sys

DEFAULT_FILTERS = [
    r"l3\.slices_0$",
    r"l3\.slices_0\.(sinka|sinkb|sinkc|sourcea|sourceb|sourced|b_arb|io_out_a_q|probehelperopt|mshralloc)$",
    r"l3\.slices_0\.ms_(0|15)$",
    r"l2\.slices_0$",
    r"l2\.slices_0\.(sinkb|sinkc|sourceb|directory|mshrctl)$",
    r"l2\.slices_0\.mshrctl\.mshrs_15$",
]


def read_totals(report_dir):
    """session.xml: <metric name="Line" value="10689/11896"/> at top scope."""
    path = os.path.join(report_dir, "session.xml")
    if not os.path.isfile(path):
        return None
    xml = open(path, encoding="utf-8", errors="replace").read()
    scope = re.search(r'<scope type="top" name="top">(.*?)</scope>', xml, re.S)
    if not scope:
        return None
    totals = {}
    for name, cov, tot in re.findall(
            r'<metric name="(\w+)" value="(\d+)/(\d+)"\s*/>', scope.group(1)):
        totals[name] = (int(cov), int(tot))
    return totals


def read_module_tree(report_dir):
    """Instance tree (hierarchy1.html) -> {flattened_path: (score, line, cond,
    toggle, fsm)}; falls back to the module-level tree in modlist.html."""
    path = os.path.join(report_dir, "hierarchy1.html")
    if not os.path.isfile(path):
        path = os.path.join(report_dir, "modlist.html")
    if not os.path.isfile(path):
        return {}
    html = open(path, encoding="utf-8", errors="replace").read()
    rows = re.findall(
        r'<tr data-tt-id="([^"]+)"(?: data-tt-parent-id="([^"]+)")?>(.*?)</tr>',
        html, re.S)
    nodes = {}
    for row_id, parent_id, body in rows:
        name_m = re.search(r"<a [^>]*>([^<]+)</a>", body)
        if not name_m:
            continue
        name = name_m.group(1).strip()
        cells = re.findall(r"<td[^>]*>(?:<a[^>]*>)?\s*([\d.]+)\s*(?:</a>)?</td>", body)
        scores = [float(c) if c else None for c in cells[:5]]
        nodes[row_id] = (parent_id or None, name, scores)

    tree = {}
    for row_id, (parent_id, name, scores) in nodes.items():
        parts, node, depth = [name], row_id, 0
        while parent_id in nodes and depth < 32:
            parts.append(nodes[parent_id][1])
            node = parent_id
            parent_id = nodes[parent_id][0]
            depth += 1
        tree[".".join(reversed(parts)).lower()] = scores
    return tree


def fmt(scores):
    if not scores:
        return "     -     "
    return "/".join("  - " if s is None else f"{s:5.1f}" for s in scores[:1] + scores[1:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("report_a")
    ap.add_argument("report_b")
    ap.add_argument("--filter", action="append", default=None,
                    help="regex on flattened module path (repeatable)")
    args = ap.parse_args()

    for d in (args.report_a, args.report_b):
        if not os.path.isdir(d):
            sys.exit(f"error: {d} is not a report directory")

    tot_a, tot_b = read_totals(args.report_a), read_totals(args.report_b)
    print("== Overall (session.xml, covered/total) ==")
    print(f"{'metric':<8} {os.path.basename(os.path.dirname(args.report_a)):<22} "
          f"{os.path.basename(os.path.dirname(args.report_b)):<22}")
    for m in ("Line", "Cond", "Toggle", "FSM"):
        a = tot_a.get(m) if tot_a else None
        b = tot_b.get(m) if tot_b else None
        pa = f"{a[0]/a[1]*100:5.2f}% ({a[0]}/{a[1]})" if a else "-"
        pb = f"{b[0]/b[1]*100:5.2f}% ({b[0]}/{b[1]})" if b else "-"
        delta = ""
        if a and b:
            da = (b[0] / b[1] - a[0] / a[1]) * 100
            delta = f"  {'+' if da >= 0 else ''}{da:.2f}pt"
        print(f"{m:<8} {pa:<22} {pb:<22}{delta}")

    tree_a = read_module_tree(args.report_a)
    tree_b = read_module_tree(args.report_b)
    patterns = args.filter or DEFAULT_FILTERS
    regexes = [re.compile(p, re.I) for p in patterns]
    keys = sorted(k for k in set(tree_a) | set(tree_b)
                  if any(r.search(k) for r in regexes))
    if not keys:
        print("\n(no modules matched the filters; paths available start with e.g.:)")
        for k in list(tree_a)[:10]:
            print("  ", k)
        return
    print("\n== Modules (score/line/cond/toggle/fsm) ==")
    print(f"{'path':<58} {'A':<30} {'B':<30}")
    for k in keys:
        print(f"{k:<58.58} {fmt(tree_a.get(k)):<30} {fmt(tree_b.get(k)):<30}")


if __name__ == "__main__":
    main()
