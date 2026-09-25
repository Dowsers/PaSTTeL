#!/usr/bin/env python3
"""
Standalone, sequential re-verification of the 39 ARRAY_OP instances that are
UNKNOWN or TIMEOUT in stats/vmcai26/results_P-ULR-Par7_z3.csv, against the
current bin/pasttel build (stem->loop pointer-distinctness fix included).

Sequential ONLY (one pasttel process at a time) -- array-heavy instances can
each use 2-3+ GB RAM; running this batch in parallel can exhaust RAM/swap.

Usage:
    python3 scripts/verify_stem_bridge_fix.py [--timelimit SECONDS] [--out FILE]

Run from the repo root (paths below are relative to it).
"""
import argparse
import csv
import os
import re
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(REPO_ROOT, "stats/vmcai26/results_P-ULR-Par7_z3.csv")
PASTTEL_BIN = os.path.join(REPO_ROOT, "bin/pasttel")


def load_target_instances():
    """UNKNOWN/TIMEOUT rows (z3 column) from the CSV, mapped to local JSON paths."""
    targets = []
    with open(CSV_PATH) as f:
        for row in csv.DictReader(f):
            status = row["PaSTTeL Status"]
            if status not in ("UNKNOWN", "TIMEOUT"):
                continue
            m = re.search(r"ARRAY_OP/(lasso_traces_[^/]+)/lasso_trace_(\d+)\.txt$", row["Trace Name"])
            if not m:
                continue
            dirname, n = m.group(1), m.group(2)
            local_path = os.path.join(REPO_ROOT, f"stats/vmcai26/ARRAY_OP/{dirname}/lasso_trace_{n}.json")
            if os.path.exists(local_path):
                targets.append((local_path, status))
    return targets


def run_one(json_path, timelimit):
    cmd = [PASTTEL_BIN, "-a", "both", "-s", "z3", "-c", "1", "-t", str(timelimit), json_path]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timelimit + 15)
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        def as_text(x):
            if x is None:
                return ""
            return x.decode("utf-8", errors="replace") if isinstance(x, bytes) else x
        output = as_text(e.stdout) + as_text(e.stderr)

    overall_match = re.search(r"OVERALL RESULT:\s*(\S.*)", output)
    overall = overall_match.group(1).strip() if overall_match else "(no OVERALL RESULT line)"
    error_match = re.search(r"^Error:.*", output, re.MULTILINE)
    error = error_match.group(0) if error_match else ""
    return overall, error


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timelimit", type=int, default=90,
                         help="Per-instance time budget in seconds (default: 90)")
    parser.add_argument("--out", default=None,
                         help="Optional path to also write raw results to (one line per instance)")
    args = parser.parse_args()

    if not os.path.exists(PASTTEL_BIN):
        print(f"ERROR: {PASTTEL_BIN} not found -- build with `make -j4` first.", file=sys.stderr)
        sys.exit(1)

    targets = load_target_instances()
    print(f"{len(targets)} instances to re-verify (sequential, {args.timelimit}s budget each)\n")

    out_f = open(args.out, "w") if args.out else None
    results = []
    start_all = time.time()
    for i, (json_path, before_status) in enumerate(targets, 1):
        rel = os.path.relpath(json_path, REPO_ROOT)
        t0 = time.time()
        overall, error = run_one(json_path, args.timelimit)
        dt = time.time() - t0
        line = f"[{i}/{len(targets)}] ({dt:5.1f}s) {before_status:9s} -> {overall:20s} {rel}"
        if error:
            line += f"   {error}"
        print(line, flush=True)
        if out_f:
            out_f.write(f"{rel}|||{before_status}|||{overall}|||{error}\n")
            out_f.flush()
        results.append((rel, before_status, overall, error))

    if out_f:
        out_f.close()

    total_time = time.time() - start_all
    print(f"\n=== Summary ({total_time:.0f}s total) ===")
    from collections import Counter
    dist = Counter(r[2] for r in results)
    for status, count in dist.most_common():
        print(f"  {status}: {count}")

    crashes = [r for r in results if r[3]]
    if crashes:
        print(f"\n{len(crashes)} instance(s) with an Error: line (should be 0):")
        for rel, _, overall, error in crashes:
            print(f"  {rel}: {error}")
    else:
        print("\nNo crashes (0 'Error:' lines) -- as expected after the Phase 1 fix.")


if __name__ == "__main__":
    main()
