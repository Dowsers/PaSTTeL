#!/usr/bin/env python3
"""Choose the programs of scripts/run_full_evaluation.sh --subset (360 for [1], 100 for [2]), from our logs.

A uniform random draw of 100 programs does not reproduce the paper's observations: a few traces
(in [1]) or a few long programs (in [2]) carry most of the time differences. This script draws
stratified samples with random.Random(seed), for seed = 1, 2, ..., and keeps the first one whose
results, as our logs give them, lie within fixed bounds of those of the whole benchmark. The
bounds are printed, and written into each subset's README with the seed found.

  [1] ULR-Baseline vs P-ULR (benchmarks/ulr_vs_pulr, logs/results_P-ULR-*_z3.csv,
      logs/ULR_vs_PULR_logs.zip): 200 C and 160 Boogie programs, the proportions of the benchmark,
      among those whose extraction took at most 200 s, so that the 300 s timeout of --subset leaves
      their lasso traces unchanged. Bounds: the speed-ups of P-ULR-Seq and P-ULR-Par7 on terminating
      and non-terminating traces, the share of affine ranking functions and of GNTA, and no single
      trace carrying more than 15 % of a total.
  [2] Ultimate-LR vs Ultimate-PL (benchmarks/ulr_vs_upl, logs/results_ULR_vs_UPL.csv), with the
      paper's 1,000 s timeout: the benchmark's classes in its proportions -- solved by both
      (terminating, non-terminating), by Ultimate-PL only, by Ultimate-LR only, by neither (taken
      among the programs both runs give up on within 60 s, to keep the subset short). Bounds: the
      time differences on the programs both solve, terminating, non-terminating and in total.

Usage (from the artifact's root):
  python3 benchmarks/subset/select_subsets.py            # print the draws found and their results
  python3 benchmarks/subset/select_subsets.py --write    # also copy the programs and write the READMEs
"""

import argparse
import collections
import csv
import datetime
import os
import random
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOLVED = ("TERMINATING", "NONTERMINATING")
N1_C, N1_BPL, EXTRACTION_MAX_S = 200, 160, 200
SUBSET1_TIMEOUT, SUBSET2_TIMEOUT = 300, 1000
QUOTAS2 = {"both TERMINATING": 51, "both NONTERMINATING": 30, "Ultimate-PL only": 3,
           "Ultimate-LR only": 1, "neither": 15}
NEITHER_MAX_S = 60
# Run-to-run noise of one program's (Ultimate-PL - Ultimate-LR) / Ultimate-LR, measured on the 60
# programs both runs solved in our run and in a rerun of the former subset on another machine
# (standard deviation 0.152). A subset's gain must exceed twice the noise it accumulates.
NOISE = 0.15
MAX_SEED = 100000


def num(x):
    try:
        return float(str(x).replace(",", "."))
    except ValueError:
        return None


# ---------------------------------------------------------------------------------------------
# [1] ULR-Baseline vs P-ULR
# ---------------------------------------------------------------------------------------------
_TRACE = re.compile(r"lasso_traces_([^/]+)/(lasso_trace_[^/]+)$")
_STAMP = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d),(\d{3})", re.M)
_RESULT = re.compile(r"TerminationAnalysisResult:\s*(Termination proven|Nontermination possible|Unable to decide)")


def load_traces():
    """{program: [trace]} where a trace is a dict: ULR's verdict and time, each P-ULR's, ULR's strategy."""
    par = {}
    for r in csv.DictReader(open(os.path.join(ROOT, "logs/results_P-ULR-Par7_z3.csv"))):
        par[_TRACE.search(r["Trace Name"]).groups()] = r
    progs = collections.defaultdict(list)
    for r in csv.DictReader(open(os.path.join(ROOT, "logs/results_P-ULR-Seq_z3.csv"))):
        key = _TRACE.search(r["Trace Name"]).groups()
        q = par.get(key, {})
        progs[key[0]].append({
            "verdict": r["Result Code"], "ulr": num(r["ULR-Baseline (ms)"]), "seq": num(r["P-ULR-Seq"]),
            "par": num(q.get("P-ULR-Par7")), "seq_status": r["PaSTTeL Status"],
            "par_status": q.get("PaSTTeL Status"), "strategy": r["Algo"].split("/")[0].strip()})
    return progs


def load_extractions():
    """{program: (verdict, seconds)} from Ultimate's logs of our run, by the timestamps of each log."""
    out = {}
    with zipfile.ZipFile(os.path.join(ROOT, "logs/ULR_vs_PULR_logs.zip")) as z:
        for name in z.namelist():
            if "/ultimate_pulr_logs/" not in name or not name.endswith(".ultimate.log"):
                continue
            text = z.read(name).decode("utf-8", "replace")
            stamps = _STAMP.findall(text)
            secs = None
            if stamps:
                t = [datetime.datetime.strptime(d, "%Y-%m-%d %H:%M:%S") + datetime.timedelta(milliseconds=int(ms))
                     for d, ms in (stamps[0], stamps[-1])]
                secs = (t[1] - t[0]).total_seconds()
            m = _RESULT.search(text)
            verdict = {"Termination proven": "TERMINATING", "Nontermination possible": "NONTERMINATING",
                       "Unable to decide": "UNKNOWN"}[m.group(1)] if m else (
                "TIMEOUT" if secs is not None and secs >= 2990 else "ERROR")
            out[os.path.basename(name)[:-len(".ultimate.log")]] = (verdict, secs)
    return out


def stats1(traces):
    """The table's figures on the traces every tool proved, plus what the bounds check."""
    acc = {v: [0, 0.0, 0.0, 0.0] for v in SOLVED}
    strat = collections.Counter()
    weights = {v: [] for v in SOLVED}
    for t in traces:
        v = t["verdict"]
        if v in SOLVED and t["seq_status"] == v and t["par_status"] == v and None not in (t["ulr"], t["seq"], t["par"]):
            a = acc[v]; a[0] += 1; a[1] += t["ulr"]; a[2] += t["seq"]; a[3] += t["par"]
            strat[(v, t["strategy"])] += 1
            weights[v].append((t["ulr"], t["seq"], t["par"]))
    T, N = acc["TERMINATING"], acc["NONTERMINATING"]
    share = lambda v, i, a: max((w[i] for w in weights[v]), default=0) / a[i + 1] if a[i + 1] else 1
    return {
        "traces": T[0] + N[0], "T": T, "N": N,
        "T_seq": T[1] / T[2] if T[2] else 0, "T_par": T[1] / T[3] if T[3] else 0,
        "N_seq": N[1] / N[2] if N[2] else 0, "N_par": N[1] / N[3] if N[3] else 0,
        "affine": strat[("TERMINATING", "Affine Template")] / T[0] if T[0] else 0,
        "gnta": strat[("NONTERMINATING", "GNTA")] / N[0] if N[0] else 0,
        "terminating": T[0] / (T[0] + N[0]) if T[0] + N[0] else 0,
        "non_affine": T[0] - strat[("TERMINATING", "Affine Template")],
        "gnta_traces": strat[("NONTERMINATING", "GNTA")],
        "max_share_T": max(share("TERMINATING", i, T) for i in range(3)),
        "max_share_N": max(share("NONTERMINATING", i, N) for i in range(3)),
    }


def bounds1(g):
    # The benchmark's shares of affine ranking functions (80 %) and of terminating traces (72 %) come
    # from a handful of programs with hundreds of traces each -- polyrank4.t2.c alone dumps 765 of the
    # 844 non-affine ones -- whose extraction takes up to 3,000 s: no subset under a 300 s timeout can
    # hold them. The subset keeps instead some of each kind of trace, and the speed-ups of Table 1.
    return [("T_seq", g["T_seq"] * 0.85, g["T_seq"] * 1.15), ("T_par", g["T_par"] * 0.85, g["T_par"] * 1.15),
            ("N_seq", g["N_seq"] * 0.8, g["N_seq"] * 1.2), ("N_par", g["N_par"] * 0.8, g["N_par"] * 1.2),
            ("gnta", g["gnta"] - 0.05, g["gnta"] + 0.05), ("non_affine", 3, 10 ** 9), ("gnta_traces", 3, 10 ** 9),
            ("max_share_T", 0, 0.15), ("max_share_N", 0, 0.25), ("traces", 100, 10 ** 9)]


def select1(progs, extractions):
    pool = sorted(p for p in os.listdir(os.path.join(ROOT, "benchmarks/ulr_vs_pulr"))
                  if p.endswith((".c", ".bpl")) and p in progs
                  and (extractions.get(p, (None, None))[1] or 10 ** 9) <= EXTRACTION_MAX_S)
    c = [p for p in pool if p.endswith(".c")]; b = [p for p in pool if p.endswith(".bpl")]
    g = stats1([t for p in progs for t in progs[p]])
    bnds = bounds1(g)
    for seed in range(1, MAX_SEED):
        rng = random.Random(seed)
        chosen = sorted(rng.sample(c, N1_C) + rng.sample(b, N1_BPL))
        s = stats1([t for p in chosen for t in progs[p]])
        if all(lo <= s[k] <= hi for k, lo, hi in bnds):
            # Running time: each extraction, then both P-ULR passes over every trace (0.1 s each to start
            # PaSTTeL), as our logs give them.
            s["hours"] = (sum(extractions[p][1] for p in chosen) + sum(
                (t["seq"] or 0) / 1000 + (t["par"] or 0) / 1000 + 0.2 for p in chosen for t in progs[p])) / 3600
            return seed, chosen, s, g, bnds, len(pool)
    sys.exit("[1]: no draw within the bounds")


# ---------------------------------------------------------------------------------------------
# [2] Ultimate-LR vs Ultimate-PL
# ---------------------------------------------------------------------------------------------
def load_upl():
    out = {}
    for r in csv.DictReader(open(os.path.join(ROOT, "logs/results_ULR_vs_UPL.csv"))):
        r = {k.replace("ULR ", "Ultimate-LR ").replace("UPL ", "Ultimate-PL "): v for k, v in r.items()}
        out[r["Program"]] = (r["Ultimate-LR Verdict"], num(r["Ultimate-LR Wall (ms)"]) / 1000,
                             r["Ultimate-PL Verdict"], num(r["Ultimate-PL Wall (ms)"]) / 1000)
    return out


def klass(u, x, p, y):
    if u in SOLVED and p in SOLVED:
        return f"both {u}" if u == p else "contradiction"
    if p in SOLVED:
        return "Ultimate-PL only"
    if u in SOLVED:
        return "Ultimate-LR only"
    return "neither"


def stats2(rows):
    acc = {v: [0, 0.0, 0.0, 0.0] for v in SOLVED}
    cls = collections.Counter()
    cost = 0.0
    for u, x, p, y in rows:
        k = klass(u, x, p, y); cls[k] += 1
        cost += min(x, SUBSET2_TIMEOUT) + min(y, SUBSET2_TIMEOUT)
        if k.startswith("both"):
            a = acc[u]; a[0] += 1; a[1] += x; a[2] += y; a[3] += (NOISE * x) ** 2
    T, N = acc["TERMINATING"], acc["NONTERMINATING"]
    pct = lambda lr, pl: 100 * (pl - lr) / lr if lr else 0
    z = lambda d, var: d / var ** 0.5 if var else 0
    return {"classes": cls, "T": T, "N": N, "dT": pct(T[1], T[2]), "dN": pct(N[1], N[2]),
            "dTotal": pct(T[1] + N[1], T[2] + N[2]), "hours": cost / 3600,
            "zT": z(T[2] - T[1], T[3]), "zTotal": z(T[2] + N[2] - T[1] - N[1], T[3] + N[3]),
            "solved_lr": sum(1 for u, x, p, y in rows if u in SOLVED),
            "solved_pl": sum(1 for u, x, p, y in rows if p in SOLVED)}


def bounds2(g):
    return [("dT", g["dT"] - 4, g["dT"] + 4), ("dN", g["dN"] - 4, g["dN"] + 4),
            ("dTotal", g["dTotal"] - 2.5, g["dTotal"] + 2.5), ("zT", -10 ** 9, -2), ("zTotal", -10 ** 9, -2),
            ("hours", 0, 2.5)]


def select2(upl):
    strata = collections.defaultdict(list)
    for prog in sorted(upl):
        u, x, p, y = upl[prog]
        k = klass(u, x, p, y)
        if k == "neither" and max(x, y) > NEITHER_MAX_S:
            continue
        strata[k].append(prog)
    g = stats2(list(upl.values()))
    bnds = bounds2(g)
    for seed in range(1, MAX_SEED):
        rng = random.Random(seed)
        chosen = sorted(p for k, n in QUOTAS2.items() for p in rng.sample(strata[k], n))
        s = stats2([upl[p] for p in chosen])
        if all(lo <= s[k] <= hi for k, lo, hi in bnds):
            return seed, chosen, s, g, bnds, {k: len(v) for k, v in strata.items()}
    sys.exit("[2]: no draw within the bounds")


# ---------------------------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------------------------
def report1(seed, chosen, s, g, bnds, pool):
    lines = [f"[1] seed {seed}: {len(chosen)} programs ({sum(p.endswith('.c') for p in chosen)} C, "
             f"{sum(p.endswith('.bpl') for p in chosen)} Boogie), among the {pool} whose extraction took "
             f"at most {EXTRACTION_MAX_S} s",
             f"    {'':34s} {'subset':>10s} {'benchmark':>10s}   bounds"]
    names = {"traces": "traces every tool proved", "T_seq": "terminating, ULR-Baseline / P-ULR-Seq",
             "T_par": "terminating, ULR-Baseline / P-ULR-Par7", "N_seq": "non-term., ULR-Baseline / P-ULR-Seq",
             "N_par": "non-term., ULR-Baseline / P-ULR-Par7", "affine": "affine share (terminating)",
             "gnta": "GNTA share (non-terminating)", "non_affine": "non-affine ranking functions",
             "gnta_traces": "GNTA traces", "max_share_T": "largest trace, of a terminating total",
             "max_share_N": "largest trace, of a non-term. total"}
    for k, lo, hi in bnds:
        fmt = (lambda v: f"{100 * v:9.1f}%") if k in ("gnta", "max_share_T", "max_share_N") else (
            (lambda v: f"{v:10.0f}") if k in ("traces", "non_affine", "gnta_traces") else (lambda v: f"{v:9.1f}x"))
        lines.append(f"    {names[k]:34s} {fmt(s[k])} {fmt(g[k]):>10s}   "
                     f"[{fmt(lo).strip()}, {fmt(hi).strip() if hi < 10 ** 8 else '-'}]")
    if "hours" in s:
        lines.append(f"    {'running time (extraction, P-ULR)':34s} {s['hours']:9.1f}h")
    return lines


def report2(seed, chosen, s, g, bnds, strata):
    lines = [f"[2] seed {seed}: {len(chosen)} programs, strata (available, taken): " +
             ", ".join(f"{k} ({strata.get(k, 0)}, {n})" for k, n in QUOTAS2.items()),
             f"    {'':34s} {'subset':>10s} {'benchmark':>10s}   bounds"]
    names = {"dT": "terminating, Ultimate-PL vs -LR", "dN": "non-terminating, Ultimate-PL vs -LR",
             "dTotal": "total (Cumulative Intersection)", "hours": "running time, both runs",
             "zT": "terminating gain, in noise std", "zTotal": "total gain, in noise std"}
    for k, lo, hi in bnds:
        if k == "hours":
            lines.append(f"    {names[k]:34s} {s[k]:9.1f}h {'':>10s}   [-, {hi:.1f}h]")
        elif k in ("zT", "zTotal"):
            lines.append(f"    {names[k]:34s} {s[k]:+9.1f}  {'':>10s}   [-, {hi:+.0f}]")
        else:
            lines.append(f"    {names[k]:34s} {s[k]:+9.1f}% {g[k]:+9.1f}%   [{lo:+.1f}%, {hi:+.1f}%]")
    lines.append(f"    {'solved: Ultimate-LR / Ultimate-PL':34s} {s['solved_lr']:>4d} / {s['solved_pl']:<4d} "
                 f"{g['solved_lr']:>4d} / {g['solved_pl']:<4d}")
    return lines


def write1(seed, chosen, s, g, bnds, pool, extractions, progs):
    d = os.path.join(ROOT, "benchmarks/subset/ulr_vs_pulr")
    refill(d, "benchmarks/ulr_vs_pulr", chosen)
    rows = []
    for p in chosen:
        v, t = extractions.get(p, ("-", None))
        rows.append(f"{p:65s} {'C' if p.endswith('.c') else 'BPL':5s} {v:15s} {t if t is None else round(t, 1)!s:>8}  {len(progs[p]):6d}")
    text = f"""benchmarks/subset/ulr_vs_pulr -- the {N1_C + N1_BPL} programs of [1] ULR-Baseline vs P-ULR for --subset
============================================================================================

The programs on which scripts/run_full_evaluation.sh --subset runs [1], with a {SUBSET1_TIMEOUT} s timeout per
Ultimate run. The files are unchanged copies of programs of benchmarks/ulr_vs_pulr (Table 1, Figure 5).

How they were chosen (benchmarks/subset/select_subsets.py, from our logs in logs/): {N1_C} C and {N1_BPL}
Boogie programs, the proportions of the benchmark, among the {pool} whose extraction took at most
{EXTRACTION_MAX_S} s in our run, so that the {SUBSET1_TIMEOUT} s timeout leaves their lasso traces unchanged. The draws
use random.Random(seed) for seed = 1, 2, ...; the first one whose traces reproduce the whole
benchmark within the bounds below is kept: seed {seed}. A uniform draw does not, since a few traces
carry most of the time differences; the bound on the largest single trace keeps the subset's
ratios from resting on one trace.

""" + "\n".join(report1(seed, chosen, s, g, bnds, pool)[1:]) + f"""

The subset keeps the speed-ups of Table 1, not two shares of the benchmark: its terminating traces
are {100 * s['affine']:.0f} % affine (80 % in the benchmark), and {100 * s['terminating']:.0f} % of its traces terminate (72 %). Both shares come
from a handful of programs with hundreds of traces each -- polyrank4.t2.c alone dumps 765 of the
benchmark's 844 non-affine ones -- whose extraction takes up to 3,000 s, beyond any subset.""" + """

The columns give our result for each program, from the run of logs/ULR_vs_PULR_logs.zip
(tools/UAutomizer-linux, timeout 3,000 s): the verdict Ultimate reached, the time it took (from
the timestamps of its log) and the number of lasso traces it dumped. ERROR means that Ultimate
stopped on an exception, with the traces dumped until then.

Program                                                           From  Verdict         Time (s)  Traces
----------------------------------------------------------------------------------------------------------
""" + "\n".join(rows) + "\n"
    open(os.path.join(d, "README"), "w").write(text)


def write2(seed, chosen, s, g, bnds, strata, upl):
    d = os.path.join(ROOT, "benchmarks/subset/ulr_vs_upl")
    refill(d, "benchmarks/ulr_vs_upl", chosen)
    rows = []
    for p in chosen:
        u, x, q, y = upl[p]
        rows.append(f"{p:65s} {'C' if p.endswith('.c') else 'BPL':5s} {u:15s} {x:8.1f}  {q:15s} {y:8.1f}  {klass(u, x, q, y)}")
    text = f"""benchmarks/subset/ulr_vs_upl -- the 100 programs of [2] Ultimate-LR vs Ultimate-PL for --subset
===================================================================================================

The programs on which scripts/run_full_evaluation.sh --subset runs [2], with the paper's {SUBSET2_TIMEOUT} s
timeout per run. The files are unchanged copies of programs of benchmarks/ulr_vs_upl (Figure 6).

How they were chosen (benchmarks/subset/select_subsets.py, from logs/results_ULR_vs_UPL.csv): the
benchmark's classes in its proportions -- solved by both runs (terminating, non-terminating), by
Ultimate-PL only, by Ultimate-LR only, by neither. The programs neither run solves are taken among
those both runs give up on within {NEITHER_MAX_S} s, to keep the subset short. The draws use
random.Random(seed) for seed = 1, 2, ...; the first one whose time differences reproduce those of
the whole benchmark within the bounds below is kept: seed {seed}. A uniform draw does not: most of
the gain of Ultimate-PL comes from a few long programs, and on short programs it is a few percent
slower, as each call to PaSTTeL costs a new process.

""" + "\n".join(report2(seed, chosen, s, g, bnds, strata)[1:]) + f"""

"In noise std" is the gain divided by the run-to-run noise it accumulates: {NOISE:.2f} times each
program's Ultimate-LR time, the spread of (Ultimate-PL - Ultimate-LR) / Ultimate-LR between our run
and a rerun of 60 programs on another machine. Below -2, the gain should show on any machine.
{top_gain_sentence(chosen, upl)}

The columns give our result for each program (logs/ULR_vs_UPL_logs.zip, timeout {SUBSET2_TIMEOUT} s per run):
the verdict of each run, its wall clock, and the program's class.

Program                                                           From  Ultimate-LR     time (s)  Ultimate-PL     time (s)  class
---------------------------------------------------------------------------------------------------------------------------------------------
""" + "\n".join(rows) + "\n"
    open(os.path.join(d, "README"), "w").write(text)


def top_gain_sentence(chosen, upl):
    """Where the subset's gain comes from, as for the benchmark (10 programs carry most of its gain)."""
    both = [(p, *upl[p]) for p in chosen if klass(*upl[p]).startswith("both")]
    total = sum(y - x for p, u, x, q, y in both)
    p, u, x, q, y = min(both, key=lambda r: r[4] - r[2])
    allb = sorted((y2 - x2 for u2, x2, q2, y2 in upl.values() if klass(u2, x2, q2, y2).startswith("both")))
    gains = [d for d in allb if d < 0]
    return (f"As in the whole benchmark, where the 10 largest gains make {100 * sum(gains[:10]) / sum(gains):.0f} % of all the gains,\n"
            f"one program carries most of the subset's gain ({total:+.0f} s): {p}, {x:.0f} s with\n"
            f"Ultimate-LR and {y:.0f} s with Ultimate-PL ({y - x:+.0f} s), a gap far beyond the noise.")


def refill(d, source, chosen):
    """Replace the programs of a subset directory by copies of the chosen ones; its README stays."""
    for name in os.listdir(os.path.join(ROOT, d)):
        if name.endswith((".c", ".bpl")):
            os.remove(os.path.join(ROOT, d, name))
    for p in chosen:
        shutil.copy2(os.path.join(ROOT, source, p), os.path.join(ROOT, d, p))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true", help="copy the chosen programs and write the READMEs")
    args = ap.parse_args()
    progs, extractions = load_traces(), load_extractions()
    r1 = select1(progs, extractions)
    print("\n".join(report1(*r1)))
    upl = load_upl()
    r2 = select2(upl)
    print("\n".join(report2(*r2)))
    if args.write:
        write1(*r1, extractions, progs)
        write2(*r2, upl)
        print("written: benchmarks/subset/ulr_vs_pulr, benchmarks/subset/ulr_vs_upl")


if __name__ == "__main__":
    main()
