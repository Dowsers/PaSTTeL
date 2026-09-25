#!/usr/bin/env python3
"""
Sequential re-verification of the 131 JSON traces (71 source-file dirs) listed
in tmp.txt, against the current bin/pasttel build.

Sequential ONLY -- see no-parallel-pasttel-batches memory.

Usage:
    python3 scripts/verify_71_timeouts.py [--timelimit SECONDS] [--out FILE]
"""
import argparse
import os
import re
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP_TXT = os.path.join(REPO_ROOT, "tmp.txt")
PASTTEL_BIN = os.path.join(REPO_ROOT, "bin/pasttel")


def load_targets():
    targets = []
    for line in open(TMP_TXT):
        line = line.strip()
        if not line:
            continue
        m = re.search(r"ARRAY_OP/(lasso_traces_[^/]+)$", line)
        if not m:
            continue
        local_dir = os.path.join(REPO_ROOT, f"stats/vmcai26/ARRAY_OP/{m.group(1)}")
        if not os.path.isdir(local_dir):
            continue
        for fn in sorted(os.listdir(local_dir)):
            if fn.endswith(".json"):
                targets.append(os.path.join(local_dir, fn))
    return targets


def run_one(json_path, timelimit):
    cmd = [PASTTEL_BIN, "-a", "terminate", "-s", "z3", "-c", "1", "-t", str(timelimit), json_path]
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
    parser.add_argument("--timelimit", type=int, default=60)
    parser.add_argument("--out", default=None)
    args = parser.parse_args()

    if not os.path.exists(PASTTEL_BIN):
        print(f"ERROR: {PASTTEL_BIN} not found -- build with `make -j4` first.", file=sys.stderr)
        sys.exit(1)

    targets = load_targets()
    print(f"{len(targets)} instances to re-verify (sequential, {args.timelimit}s budget each)\n")

    out_f = open(args.out, "w") if args.out else None
    results = []
    start_all = time.time()
    for i, json_path in enumerate(targets, 1):
        rel = os.path.relpath(json_path, REPO_ROOT)
        t0 = time.time()
        overall, error = run_one(json_path, args.timelimit)
        dt = time.time() - t0
        line = f"[{i}/{len(targets)}] ({dt:5.1f}s) {overall:20s} {rel}"
        if error:
            line += f"   {error}"
        print(line, flush=True)
        if out_f:
            out_f.write(f"{rel}|||{overall}|||{error}\n")
            out_f.flush()
        results.append((rel, overall, error, dt))

    if out_f:
        out_f.close()

    total_time = time.time() - start_all
    print(f"\n=== Summary ({total_time:.0f}s total) ===")
    from collections import Counter
    dist = Counter(r[1] for r in results)
    for status, count in dist.most_common():
        print(f"  {status}: {count}")

    crashes = [r for r in results if r[2]]
    if crashes:
        print(f"\n{len(crashes)} instance(s) with an Error: line:")
        for rel, _, error, _ in crashes:
            print(f"  {rel}: {error}")


if __name__ == "__main__":
    main()
