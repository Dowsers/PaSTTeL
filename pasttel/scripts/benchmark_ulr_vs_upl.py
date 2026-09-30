#!/usr/bin/env python3
"""Parse paired Ultimate logs (Ultimate-LR vs Ultimate-PL) into a CSV, and plot the comparison.

Each program is analysed twice by a full Ultimate run -- once with the stock
LassoRanker rank-synthesis backend (Ultimate-LR), once with the PaSTTeL backend for
ranking functions, falling back to LassoRanker when PaSTTeL does not conclude (Ultimate-PL).
Not to be confused with ULR-Baseline, LassoRanker timed on single lassos in ULR-Baseline vs P-ULR.
scripts/run_ulr_vs_upl.sh produces, per program and per configuration (<cfg>: ulr for Ultimate-LR,
upl for Ultimate-PL):

    <name>.<cfg>.log       Ultimate stdout+stderr
    <name>.<cfg>.wall_ms   wall clock measured around the process, in ms

This module turns those into one row per program. Three timings are recorded
side by side because they answer different questions: wall clock includes JVM
startup and parsing, the plugin time isolates BuchiAutomizer, and the lasso
analysis time isolates the part PaSTTeL actually replaces.
"""

import argparse
import collections
import csv
import glob
import os
import re
import statistics
import sys

CONFIGS = ("ulr", "upl")
# The two configurations, as the paper names them, in the table, the CSV and the plot.
LR, PL = "Ultimate-LR", "Ultimate-PL"
SIDES = (LR, PL)

# Axis and column labels. A scatter of two Ultimate runs is unreadable unless it
# says which backend each axis is: both runs are "Ultimate", and the only
# difference lives in the settings file. run_ulr_vs_upl.sh overrides these with
# the .epf names it actually used.
DEFAULT_ULR_LABEL = f"{LR} — LassoRanker backend"
DEFAULT_UPL_LABEL = f"{PL} — PaSTTeL backend"

# --- Verdicts ---------------------------------------------------------------
# TerminationAnalysisResult.getShortDescription() in the Ultimate sources.
_VERDICT_PATTERNS = (
    ("TERMINATING", re.compile(r"TerminationAnalysisResult:\s*Termination proven")),
    ("NONTERMINATING", re.compile(r"TerminationAnalysisResult:\s*Nontermination possible")),
    ("UNKNOWN", re.compile(r"TerminationAnalysisResult:\s*Unable to decide termination")),
)
# No verdict: the run hit the time limit (TIMEOUT), or it ended early without one, e.g. on an
# exception, which counts as UNKNOWN like Ultimate's own "Unable to decide termination". The time
# limit is decided by timeout(1)'s exit status, which run_ulr_vs_upl.sh keeps in <name>.<cfg>.exit;
# the exception Ultimate logs cannot tell, since a run stopped at the time limit may raise one while it
# shuts down. Logs recorded before that sidecar existed fall back on Ultimate's own TimeoutResult,
# then on a wall clock (<name>.<cfg>.wall_ms) that reached the limit.
TIMEOUT = "TIMEOUT"
UNKNOWN = "UNKNOWN"
TIMEOUT_EXIT_STATUS = "124"
_TIMEOUT_RESULT_RE = re.compile(r"TimeoutResult")

# --- Timings ----------------------------------------------------------------
# Ultimate prints decimal separators according to the JVM locale, so a French
# locale yields "1376,85ms" where an English one yields "1376.85ms". Both are
# accepted everywhere a number is read.
_NUM = r"(\d+(?:[.,]\d+)?)"
_PLUGIN_RE = re.compile(r"BüchiAutomizer plugin needed " + _NUM + r"\s*s and (\d+) iterations")
_PLUGIN_ASCII_RE = re.compile(r"B\S*chiAutomizer plugin needed " + _NUM + r"\s*s and (\d+) iterations")
_LASSOS_RE = re.compile(r"Analysis of lassos took " + _NUM + r"\s*s")
_TOOLCHAIN_RE = re.compile(r"BuchiAutomizer took " + _NUM + r"\s*ms")

# --- PaSTTeL backend activity (Ultimate-PL logs only) -------------------------------
# All emitted by LassoCheck; see BuchiAutomizer/.../LassoCheck.java.
# Termination logs "SUCCESS via <technique>, ranking function <rf>", while
# non-termination stops after the technique -- so stop at a comma, not at
# whitespace, or the technique keeps the separator.
_PASTTEL_SUCCESS_RE = re.compile(r"PaSTTeL (?:termination|non-termination) check: SUCCESS via ([^,\n]+)")
# A PaSTTeL call that produced no argument (UNKNOWN, timeout, crash, unmapped certificate), after which
# LassoCheck falls back to LassoRanker ("falling back to LassoRanker"; a build without that fallback logs
# "no ranking function" for the same event).
_PASTTEL_NO_RESULT_RE = re.compile(r"PaSTTeL .*(?:no ranking function|falling back to LassoRanker)")
_PASTTEL_INVOKE_FAIL_RE = re.compile(r"PaSTTeL invocation failed")
_PASTTEL_UNMAPPED_RE = re.compile(r"PaSTTeL reported \S+ but its certificate could not be mapped back")

COLUMNS = [
    "Program", "Ext", "Timeout (s)",
    f"{LR} Verdict", f"{LR} Wall (ms)", f"{LR} Plugin (ms)", f"{LR} Lassos (ms)", f"{LR} Iters",
    f"{PL} Verdict", f"{PL} Wall (ms)", f"{PL} Plugin (ms)", f"{PL} Lassos (ms)", f"{PL} Iters",
    f"{PL} PaSTTeL Calls", f"{PL} PaSTTeL Success", f"{PL} No Result", f"{PL} Unmapped",
    f"{PL} Techniques",
    "Agreement", "Speedup (wall)", "Speedup (plugin)",
]

SOLVED = ("TERMINATING", "NONTERMINATING")
MISSING = "NO LOG"
# How run_ulr_vs_upl.sh reports each verdict next to a run's wall clock (--verdict).
VERDICT_LABELS = {
    "TERMINATING": "proved terminating", "NONTERMINATING": "proved nonterminating",
    UNKNOWN: UNKNOWN, TIMEOUT: TIMEOUT, MISSING: MISSING,
}


def _num(text):
    """Parse a number written with either decimal separator."""
    return float(text.replace(",", "."))


def _sidecar(log_path, ext):
    """Content of the file run_ulr_vs_upl.sh writes next to <name>.<cfg>.log, or None."""
    base = log_path[:-len(".log")] if log_path.endswith(".log") else log_path
    try:
        with open(base + ext) as fh:
            return fh.read().strip()
    except OSError:
        return None


def _hit_time_limit(log_path, text, timeout_s):
    """True when the time limit stopped the run, rather than the run ending on its own."""
    status = _sidecar(log_path, ".exit")
    if status is not None:
        return status == TIMEOUT_EXIT_STATUS
    if _TIMEOUT_RESULT_RE.search(text):
        return True
    try:
        return float(_sidecar(log_path, ".wall_ms")) >= timeout_s * 1000.0
    except (TypeError, ValueError):
        return False


def parse_log(path, timeout_s):
    """Extract verdict, timings and PaSTTeL counters from one Ultimate log."""
    out = {
        "verdict": "UNKNOWN", "plugin_ms": None, "lassos_ms": None,
        "toolchain_ms": None, "iterations": None,
        "pasttel_success": 0, "pasttel_no_result": 0,
        "pasttel_invoke_fail": 0, "pasttel_unmapped": 0, "techniques": [],
    }
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        out["verdict"] = MISSING
        return out

    for verdict, pattern in _VERDICT_PATTERNS:
        if pattern.search(text):
            out["verdict"] = verdict
            break
    else:
        # No TerminationAnalysisResult at all: the run did not reach a verdict.
        out["verdict"] = TIMEOUT if _hit_time_limit(path, text, timeout_s) else UNKNOWN

    m = _PLUGIN_RE.search(text) or _PLUGIN_ASCII_RE.search(text)
    if m:
        out["plugin_ms"] = _num(m.group(1)) * 1000.0
        out["iterations"] = int(m.group(2))
    m = _LASSOS_RE.search(text)
    if m:
        out["lassos_ms"] = _num(m.group(1)) * 1000.0
    m = _TOOLCHAIN_RE.search(text)
    if m:
        out["toolchain_ms"] = _num(m.group(1))

    out["techniques"] = _PASTTEL_SUCCESS_RE.findall(text)
    out["pasttel_success"] = len(out["techniques"])
    out["pasttel_invoke_fail"] = len(_PASTTEL_INVOKE_FAIL_RE.findall(text))
    out["pasttel_no_result"] = len(_PASTTEL_NO_RESULT_RE.findall(text))
    out["pasttel_unmapped"] = len(_PASTTEL_UNMAPPED_RE.findall(text))
    return out


def read_wall_ms(log_dir, name, cfg):
    """Wall clock as measured by the shell script, not derived from the log."""
    path = os.path.join(log_dir, f"{name}.{cfg}.wall_ms")
    try:
        with open(path) as fh:
            return float(fh.read().strip())
    except (OSError, ValueError):
        return None


def discover_programs(log_dir):
    """Program names that have at least one .log in the directory."""
    names = set()
    for cfg in CONFIGS:
        for path in glob.glob(os.path.join(log_dir, f"*.{cfg}.log")):
            names.add(os.path.basename(path)[: -len(f".{cfg}.log")])
    return sorted(names)


def fmt(value, digits=2):
    return "-" if value is None else f"{value:.{digits}f}"


def speedup(ulr, upl):
    """How many times faster Ultimate-PL is than Ultimate-LR. None when either side is missing."""
    if ulr is None or upl is None or upl <= 0:
        return None
    return ulr / upl


def agreement(ulr_verdict, upl_verdict):
    # A side that was not run at all (e.g. an interrupted run) is missing data,
    # not a disagreement: saying otherwise would report every row of a one-sided
    # run as a verdict conflict.
    if ulr_verdict == MISSING and upl_verdict == MISSING:
        return "NO DATA"
    if ulr_verdict == MISSING:
        return f"{LR} MISSING"
    if upl_verdict == MISSING:
        return f"{PL} MISSING"
    u_ok, p_ok = ulr_verdict in SOLVED, upl_verdict in SOLVED
    if u_ok and p_ok:
        # The only disagreement that is a conflict: both concluded, with opposite verdicts.
        return "SAME" if ulr_verdict == upl_verdict else "CONFLICT"
    if p_ok:
        return f"{PL} ONLY"
    if u_ok:
        return f"{LR} ONLY"
    # Neither solved: a TIMEOUT against an UNKNOWN run is two failures, not a conflict.
    return f"BOTH {ulr_verdict}" if ulr_verdict == upl_verdict else "NEITHER"


def build_rows(log_dir, timeout_s):
    rows = []
    for name in discover_programs(log_dir):
        parsed = {cfg: parse_log(os.path.join(log_dir, f"{name}.{cfg}.log"), timeout_s)
                  for cfg in CONFIGS}
        wall = {cfg: read_wall_ms(log_dir, name, cfg) for cfg in CONFIGS}
        u, p = parsed["ulr"], parsed["upl"]

        # A speedup only means something when both runs solved the program: a run that gave up or
        # stopped early is not faster, it did not answer.
        both_solved = u["verdict"] in SOLVED and p["verdict"] in SOLVED

        rows.append({
            "Program": name,
            "Ext": name.rsplit(".", 1)[-1] if "." in name else "-",
            "Timeout (s)": timeout_s,
            f"{LR} Verdict": u["verdict"],
            f"{LR} Wall (ms)": fmt(wall["ulr"]),
            f"{LR} Plugin (ms)": fmt(u["plugin_ms"]),
            f"{LR} Lassos (ms)": fmt(u["lassos_ms"]),
            f"{LR} Iters": u["iterations"] if u["iterations"] is not None else "-",
            f"{PL} Verdict": p["verdict"],
            f"{PL} Wall (ms)": fmt(wall["upl"]),
            f"{PL} Plugin (ms)": fmt(p["plugin_ms"]),
            f"{PL} Lassos (ms)": fmt(p["lassos_ms"]),
            f"{PL} Iters": p["iterations"] if p["iterations"] is not None else "-",
            f"{PL} PaSTTeL Calls": p["pasttel_success"] + p["pasttel_no_result"],
            f"{PL} PaSTTeL Success": p["pasttel_success"],
            f"{PL} No Result": p["pasttel_no_result"],
            f"{PL} Unmapped": p["pasttel_unmapped"],
            f"{PL} Techniques": "|".join(sorted(set(p["techniques"]))) or "-",
            "Agreement": agreement(u["verdict"], p["verdict"]),
            "Speedup (wall)": fmt(speedup(wall["ulr"], wall["upl"]) if both_solved else None, 3),
            "Speedup (plugin)": fmt(speedup(u["plugin_ms"], p["plugin_ms"]) if both_solved else None, 3),
        })
    return rows


def write_csv(rows, path):
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=COLUMNS)
        writer.writeheader()
        writer.writerows(rows)


def read_csv(path):
    """The rows of a CSV this script wrote. Columns named "ULR ..." / "UPL ..." by earlier versions are
    read under their current names, "Ultimate-LR ..." / "Ultimate-PL ..."."""
    old = {"ULR ": f"{LR} ", "UPL ": f"{PL} "}
    with open(path, newline="") as fh:
        rows = list(csv.DictReader(fh))
    return [{next((new + k[len(o):] for o, new in old.items() if k.startswith(o)), k): v for k, v in r.items()}
            for r in rows]


def parse_float(text):
    if text is None:
        return None
    text = str(text).strip().strip('"').replace(",", ".")
    if text in ("-", ""):
        return None
    try:
        return float(text)
    except ValueError:
        return None


COLUMN_FOR = {
    "wall": (f"{LR} Wall (ms)", f"{PL} Wall (ms)", "wall clock"),
    "plugin": (f"{LR} Plugin (ms)", f"{PL} Plugin (ms)", "BuchiAutomizer plugin"),
    "lassos": (f"{LR} Lassos (ms)", f"{PL} Lassos (ms)", "lasso analysis"),
}

# The paper's table, split by verdict. A program counts as solved by a run that proved it TERMINATING
# or NONTERMINATING; TIMEOUT and UNKNOWN runs solved nothing and enter no total.
#   Ultimate-LR, Ultimate-PL  #Solved: the programs the run solved. The time columns cover the
#             programs both runs solved (the intersection), split by verdict; their Total is the
#             paper's Cumulative Intersection Time, so both runs are timed on the same programs.
#   VBS       the virtual best solver: the programs either run solved, each at the better of the two
#             times.
# A program the two runs solved with opposite verdicts is a contradiction: it is left out of the
# intersection and of VBS, and listed.
def _as_int(value):
    try:
        return int(str(value).strip())
    except ValueError:
        return 0


def vbs_table(rows, col_key="wall"):
    cx, cy, metric = COLUMN_FOR[col_key]
    runs = [r for r in rows if MISSING not in (r[f"{LR} Verdict"], r[f"{PL} Verdict"])]
    solved, failed = {}, {}
    for side, vcol, tcol in ((LR, f"{LR} Verdict", cx), (PL, f"{PL} Verdict", cy)):
        solved[side] = {r["Program"]: (r[vcol], parse_float(r[tcol]) or 0.0) for r in runs if r[vcol] in SOLVED}
        failed[side] = collections.Counter(r[vcol] for r in runs if r[vcol] not in SOLVED)
    u, p = solved[LR], solved[PL]
    contra = sorted(prog for prog in u.keys() & p.keys() if u[prog][0] != p[prog][0])
    both = {v: sorted(prog for prog in u.keys() & p.keys() if u[prog][0] == p[prog][0] == v) for v in SOLVED}
    vbs = {v: {} for v in SOLVED}
    for prog in (u.keys() | p.keys()) - set(contra):
        results = [res[prog] for res in (u, p) if prog in res]
        vbs[results[0][0]][prog] = min(ms for _, ms in results)
    secs = {side: {v: sum(solved[side][prog][1] for prog in both[v]) / 1000 for v in SOLVED} for side in solved}
    return {
        "metric": metric, "programs": len(runs), "timeout": next(
            (parse_float(r.get("Timeout (s)")) for r in runs if parse_float(r.get("Timeout (s)"))), None),
        "solved": solved, "failed": failed, "both": both, "contra": contra, "secs": secs,
        "vbs": vbs, "vbs_secs": {v: sum(vbs[v].values()) / 1000 for v in SOLVED},
        # CSVs written before "No Result" have the same count under "Fallbacks".
        "pasttel": {key: sum(_as_int(r.get(col, r.get(old, 0))) for r in runs) for key, col, old in (
            ("calls", f"{PL} PaSTTeL Calls", ""), ("ok", f"{PL} PaSTTeL Success", ""),
            ("no_result", f"{PL} No Result", f"{PL} Fallbacks"), ("unmapped", f"{PL} Unmapped", ""))},
    }


def table_lines(t):
    T, N = SOLVED
    n_both = len(t["both"][T]) + len(t["both"][N])
    head = (f"{'Tool':<11}  {'#Solved':>8}   {'#Terminating':>12}  {'Time (s)':>10}   "
            f"{'#Non-Terminating':>16}  {'Time (s)':>10}   {'Total (s)':>10}")
    sep = "-" * (len(head) + 1)
    timeout = f", Ultimate timeout {t['timeout']:g} s per run" if t["timeout"] else ""
    lines = [f"{LR} vs {PL} on {t['programs']} C/BPL programs{timeout}, {t['metric']}", sep, head, sep]
    vs = t["vbs_secs"]
    lines.append(f"{'VBS':<11}  {len(t['vbs'][T]) + len(t['vbs'][N]):>8}    {len(t['vbs'][T]):>12}  {vs[T]:>10.2f}    "
                 f"{len(t['vbs'][N]):>16}  {vs[N]:>10.2f}    {vs[T] + vs[N]:>10.2f}")
    sides = SIDES
    tot = {side: t["secs"][side][T] + t["secs"][side][N] for side in sides}
    star = lambda side, values, pick: "*" if values[side] == pick(values.values()) else " "
    n_solved = {side: len(t["solved"][side]) for side in sides}
    for side in sides:
        st, sn = ({x: t["secs"][x][v] for x in sides} for v in (T, N))
        lines.append(f"{side:<11}  {n_solved[side]:>8}{star(side, n_solved, max)}   {len(t['both'][T]):>12}  "
                     f"{t['secs'][side][T]:>10.2f}{star(side, st, min)}   {len(t['both'][N]):>16}  "
                     f"{t['secs'][side][N]:>10.2f}{star(side, sn, min)}   {tot[side]:>10.2f}{star(side, tot, min)}")
    lines += [sep,
              f"{LR}, {PL}: #Solved by each run; times on the {n_both} programs both runs solved,",
              "  Total being the Cumulative Intersection Time. VBS: the programs either run solved, each at its",
              "  better time. * best.",
              "Not solved: " + " | ".join(
                  f"{side} {sum(t['failed'][side].values())}"
                  + (" (" + ", ".join(f"{k} {n}" for k, n in t["failed"][side].most_common()) + ")"
                     if t["failed"][side] else "") for side in sides),
              f"Solved by one run only: {LR} {n_solved[LR] - n_both - len(t['contra'])}, "
              f"{PL} {n_solved[PL] - n_both - len(t['contra'])}",
              f"PaSTTeL inside {PL}: {t['pasttel']['calls']} calls, {t['pasttel']['ok']} conclusive, "
              f"{t['pasttel']['no_result']} without result (LassoRanker took over), "
              f"{t['pasttel']['unmapped']} certificates not mapped back"]
    if t["contra"]:
        lines.append(f"CONTRADICTIONS ({len(t['contra'])}), both runs solved these with opposite verdicts:")
        lines += [f"  {prog}: {LR}={t['solved'][LR][prog][0]} {PL}={t['solved'][PL][prog][0]}"
                  for prog in t["contra"]]
    else:
        lines.append("Contradictions: none")
    return lines


def sides_run(rows):
    """The sides with at least one log: both, or one after run_ulr_vs_upl.sh --only."""
    return [side for side in SIDES if any(r[f"{side} Verdict"] != MISSING for r in rows)]


def one_side_lines(rows, side, col_key="wall"):
    """The summary of a side run alone: its own counts and times, in the layout of the table."""
    cx, cy, metric = COLUMN_FOR[col_key]
    vcol, tcol = f"{side} Verdict", cx if side == LR else cy
    runs = [r for r in rows if r[vcol] != MISSING]
    T, N = SOLVED
    n = {v: sum(1 for r in runs if r[vcol] == v) for v in SOLVED}
    secs = {v: sum(parse_float(r[tcol]) or 0.0 for r in runs if r[vcol] == v) / 1000 for v in SOLVED}
    failed = collections.Counter(r[vcol] for r in runs if r[vcol] not in SOLVED)
    timeout = next((parse_float(r.get("Timeout (s)")) for r in runs if parse_float(r.get("Timeout (s)"))), None)
    head = (f"{'Tool':<11}  {'#Solved':>8}   {'#Terminating':>12}  {'Time (s)':>10}   "
            f"{'#Non-Terminating':>16}  {'Time (s)':>10}   {'Total (s)':>10}")
    sep = "-" * (len(head) + 1)
    lines = [f"{side} alone on {len(runs)} C/BPL programs"
             + (f", Ultimate timeout {timeout:g} s per run" if timeout else "") + f", {metric}", sep, head, sep,
             f"{side:<11}  {n[T] + n[N]:>8}    {n[T]:>12}  {secs[T]:>10.2f}    {n[N]:>16}  {secs[N]:>10.2f}    "
             f"{secs[T] + secs[N]:>10.2f}", sep,
             f"Times on the programs {side} solved. The other side, run with the same --output, completes the table.",
             f"Not solved: {side} {sum(failed.values())}"
             + (" (" + ", ".join(f"{k} {c}" for k, c in failed.most_common()) + ")" if failed else "")]
    if side == PL:
        calls, ok, no_result, unmapped = (sum(_as_int(r.get(col, 0)) for r in runs) for col in (
            f"{PL} PaSTTeL Calls", f"{PL} PaSTTeL Success", f"{PL} No Result", f"{PL} Unmapped"))
        lines.append(f"PaSTTeL inside {PL}: {calls} calls, {ok} conclusive, {no_result} without result "
                     f"(LassoRanker took over), {unmapped} certificates not mapped back")
    return lines


def render_summary(rows, col_key="wall"):
    """The text summary, for stdout and summary_tables.log alike: the table of both sides, or the
    summary of the one side run."""
    sides = sides_run(rows)
    if len(sides) == 1:
        return "\n".join(one_side_lines(rows, sides[0], col_key))
    return "\n".join(table_lines(vbs_table(rows, col_key)))


def _plotly_script_tag():
    try:
        import plotly as _pl
        js = os.path.join(os.path.dirname(_pl.__file__), "package_data", "plotly.min.js")
        with open(js) as fh:
            return f"<script>{fh.read()}</script>"
    except Exception:
        return '<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>'


def _esc(text):
    return str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# The scatter plot, laid out as the paper's Figure 6: one panel per verdict, terminating (left, green)
# and non-terminating (right, blue). A program both runs solved is a +; a program one run solved is a
# triangle, the run that failed (TIMEOUT or UNKNOWN) being drawn beyond the red timeout line on its
# axis; a program the two runs solved with opposite verdicts is a black star. Programs neither run
# solved are not drawn.
_PANELS = (("Terminating", "TERMINATING", "green"), ("Non-terminating", "NONTERMINATING", "blue"))
_KINDS = {"both": ("solved by both", "cross-thin"), "one": ("solved by one only", "triangle-up-open"),
          "contradiction": ("contradiction", "star")}


def scatter_points(rows, col_key, timeout_ms, log_scale):
    """The points of the scatter plot: one per program at least one run solved, in the order of the
    CSV. A run that did not solve it is placed at beyond_ms, past the timeout. Returns (points, number
    of programs neither run solved, beyond_ms), a point being a dict with program, ulr/upl verdicts,
    x/y (ms, as drawn), panel (the verdict found; Ultimate-LR's on a contradiction), kind (both, one,
    contradiction) and hover."""
    cx, cy, _ = COLUMN_FOR[col_key]
    # Where a run that did not solve the program is drawn: just past the timeout, inside the axes.
    beyond_ms = timeout_ms * (1.35 if log_scale else 1.05)
    points, neither = [], 0
    for r in rows:
        u, p = r[f"{LR} Verdict"], r[f"{PL} Verdict"]
        if MISSING in (u, p):
            continue
        u_ok, p_ok = u in SOLVED, p in SOLVED
        if not (u_ok or p_ok):
            neither += 1
            continue
        x, y = parse_float(r[cx]), parse_float(r[cy])
        if (u_ok and x is None) or (p_ok and y is None):
            continue    # a solved run without that time (no statistics line in its log)
        hx = f"{x:.1f} ms" if u_ok else f"not solved ({x:.1f} ms), drawn beyond the timeout" if x else "not solved"
        hy = f"{y:.1f} ms" if p_ok else f"not solved ({y:.1f} ms), drawn beyond the timeout" if y else "not solved"
        hover = (f"{r['Program']}<br>{LR}: {u}, {hx}<br>{PL}: {p}, {hy}"
                 f"<br>PaSTTeL conclusive/calls: {_as_int(r.get(PL + ' PaSTTeL Success', 0))}"
                 f"/{_as_int(r.get(PL + ' PaSTTeL Calls', 0))}")
        if u_ok and p_ok:
            kind, panel = ("both" if u == p else "contradiction"), u
        else:
            kind, panel = "one", (u if u_ok else p)
            x, y = (x if u_ok else beyond_ms), (y if p_ok else beyond_ms)
        points.append({"program": r["Program"], "ulr": u, "upl": p, "x": x, "y": y, "panel": panel,
                       "kind": kind, "hover": hover})
    return points, neither, beyond_ms


def plot(csv_path, col_key, output_html, log_scale, timeout_s,
         ulr_label=DEFAULT_ULR_LABEL, upl_label=DEFAULT_UPL_LABEL):
    """The summary table, then the scatter plot laid out as the paper's Figure 6 (see _PANELS)."""
    import json
    rows = read_csv(csv_path)
    t = vbs_table(rows, col_key)
    cx, cy, metric = COLUMN_FOR[col_key]
    title = f"{ulr_label} vs {upl_label}"
    timeout_ms = 1000.0 * (t["timeout"] or timeout_s)
    points, neither, beyond_ms = scatter_points(rows, col_key, timeout_ms, log_scale)
    if not points:
        sides = sides_run(rows)
        why = f"only {sides[0]} was run" if len(sides) == 1 else "no program solved"
        print(f"No scatter plot for {csv_path}: {why}", file=sys.stderr)
        return None

    # The same range on every axis of both panels, from the fastest run to just past the triangles.
    vals = [v for pt in points for v in (pt["x"], pt["y"]) if v > 0]
    lo, hi = min(vals) * 0.7, max(vals + [beyond_ms]) * (1.25 if log_scale else 1.03)
    import math
    rng = [math.log10(lo), math.log10(hi)] if log_scale else [0, hi]
    axis = {"type": "log" if log_scale else "linear", "range": rng, "constrain": "domain",
            "showgrid": True, "gridcolor": "#ddd", "zeroline": False, "mirror": True,
            "showline": True, "linecolor": "black", "ticks": "outside"}
    if log_scale:
        axis.update(dtick=1, exponentformat="power")    # one tick per decade, labelled 10^k
    traces, layout = [], {}
    for i, (panel_title, verdict, colour) in enumerate(_PANELS, 1):
        xa, ya = ("x", "y") if i == 1 else (f"x{i}", f"y{i}")
        for kind, (kind_label, symbol) in _KINDS.items():
            pts = [pt for pt in points if pt["panel"] == verdict and pt["kind"] == kind]
            if not pts:
                continue
            col = "black" if kind == "contradiction" else colour
            traces.append({
                "x": [pt["x"] for pt in pts], "y": [pt["y"] for pt in pts], "text": [pt["hover"] for pt in pts],
                "xaxis": xa, "yaxis": ya, "mode": "markers", "type": "scatter", "hoverinfo": "text",
                "name": f"{panel_title}, {kind_label} ({len(pts)})",
                # Line-only markers (+, open triangle), drawn by their outline as in the paper.
                "marker": {"color": col, "symbol": symbol, "size": 11 if symbol == "star" else 8,
                           "line": {"width": 1.3, "color": col}},
            })
        # y = x, and the timeout on each axis (red, dashed) beyond which a failed run is drawn.
        traces.append({"x": [lo, hi], "y": [lo, hi], "xaxis": xa, "yaxis": ya, "mode": "lines",
                       "type": "scatter", "showlegend": False, "hoverinfo": "skip",
                       "line": {"color": "black", "width": 1.2, "dash": "dash"}})
        traces.append({"x": [timeout_ms, timeout_ms, None, lo, hi], "y": [lo, hi, None, timeout_ms, timeout_ms],
                       "xaxis": xa, "yaxis": ya, "mode": "lines", "type": "scatter", "hoverinfo": "skip",
                       "name": f"timeout ({timeout_ms / 1000:g} s)", "showlegend": i == 1,
                       "line": {"color": "red", "width": 1.2, "dash": "dash"}})
        domain = [0.0, 0.44] if i == 1 else [0.56, 1.0]
        suffix = "" if i == 1 else str(i)
        layout[f"xaxis{suffix}"] = dict(axis, domain=domain, anchor=ya,
                                        title={"text": f"{ulr_label} — {metric} (ms)"})
        # Square panels: one decade (or one unit) is the same length on both axes.
        layout[f"yaxis{suffix}"] = dict(axis, anchor=xa, scaleanchor=xa, scaleratio=1,
                                        title={"text": f"{upl_label} — {metric} (ms)"})
        layout.setdefault("annotations", []).append(
            {"text": panel_title, "xref": f"{xa} domain", "yref": f"{ya} domain", "x": 0.5, "y": 1.08,
             "showarrow": False, "font": {"size": 15}})
    layout.update({"hovermode": "closest", "plot_bgcolor": "white",
                   "legend": {"orientation": "h", "x": 0.5, "xanchor": "center", "y": -0.2},
                   "margin": {"l": 80, "r": 30, "t": 50, "b": 110}})
    table = "\n".join(table_lines(t))
    hidden = f" The {neither} programs neither run solved are not drawn." if neither else ""
    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{_esc(title)} - Scatter Plot</title>
{_plotly_script_tag()}
<style>
  body {{ font-family: Arial, sans-serif; margin: 20px; }}
  pre {{ font-size: 0.95em; }}
  #plot {{ width: 1100px; max-width: 100%; height: 600px; }}
</style>
</head>
<body>
<h2>{_esc(title)} &mdash; {t['programs']} programs, {_esc(metric)}</h2>
<pre>{_esc(table)}</pre>
<p style="font-size:0.85em; color:#555; max-width:1100px;">
  As Figure 6 of the paper: {_esc(LR)} on the X axis, {_esc(PL)} on the Y axis, terminating programs on
  the left and non-terminating ones on the right. Programs solved by both runs are marked with +, those
  solved by only one run with a triangle, the failing run (TIMEOUT or UNKNOWN) being placed beyond the
  red timeout line on its own axis -- to the right when it was {_esc(LR)}, at the top when it was
  {_esc(PL)}.{hidden} Hover a point for both verdicts and times.
</p>
<div id="plot"></div>
<script>
Plotly.newPlot('plot', {json.dumps(traces)}, {json.dumps(layout)});
</script>
</body>
</html>"""
    os.makedirs(os.path.dirname(os.path.abspath(output_html)) or ".", exist_ok=True)
    with open(output_html, "w") as fh:
        fh.write(html)
    return output_html


def plot_path(csv_path, col_key):
    """Where the scatter plot of a CSV goes: next to it, results_ULR_vs_UPL_<col>.html."""
    return (csv_path[:-4] if csv_path.endswith(".csv") else csv_path) + f"_{col_key}.html"


def main():
    ap = argparse.ArgumentParser(description=f"{LR} vs {PL}: parse Ultimate logs, emit CSV and plots")
    ap.add_argument("--log-dir", help="Directory holding <prog>.{ulr,upl}.log files")
    ap.add_argument("--output", default="results_ULR_vs_UPL.csv", help="Output CSV (or HTML with --plot)")
    ap.add_argument("--timeout", type=int, default=600, help="Ultimate timeout used, in seconds")
    ap.add_argument("--summary-file", default=None,
                    help="Also write the summary tables to this file")
    ap.add_argument("--plot", metavar="CSV", help="Plot an existing CSV instead of parsing logs")
    ap.add_argument("--verdict", metavar="LOG",
                    help="Print the verdict of one Ultimate log, as run_ulr_vs_upl.sh reports it")
    ap.add_argument("--col", choices=sorted(COLUMN_FOR), default="wall", help="Timing column to plot")
    ap.add_argument("--log-scale", action="store_true", help="Logarithmic axes")
    ap.add_argument("--ulr-label", default=DEFAULT_ULR_LABEL,
                    help="Name of the baseline configuration, used on the x axis")
    ap.add_argument("--upl-label", default=DEFAULT_UPL_LABEL,
                    help="Name of the PaSTTeL configuration, used on the y axis")
    args = ap.parse_args()

    if args.verdict:
        print(VERDICT_LABELS[parse_log(args.verdict, args.timeout)["verdict"]])
        return

    if args.plot:
        if not os.path.isfile(args.plot):
            ap.error(f"CSV not found: {args.plot}")
        out = args.output if args.output.endswith(".html") else plot_path(args.plot, args.col)
        print(render_summary(read_csv(args.plot), args.col))
        if plot(args.plot, args.col, out, args.log_scale, args.timeout, args.ulr_label, args.upl_label):
            print(f"Scatter plot: {out}")
        return

    if not args.log_dir:
        ap.error("--log-dir is required unless --plot is given")
    if not os.path.isdir(args.log_dir):
        ap.error(f"log directory not found: {args.log_dir}")

    rows = build_rows(args.log_dir, args.timeout)
    if not rows:
        print(f"No <prog>.{{ulr,upl}}.log found in {args.log_dir}", file=sys.stderr)
        sys.exit(1)
    # The CSV, its scatter plot and the summary always go together; the summary ends with their paths.
    write_csv(rows, args.output)
    html = plot(args.output, args.col, plot_path(args.output, args.col), args.log_scale, args.timeout,
                args.ulr_label, args.upl_label)
    text = render_summary(rows, args.col)
    if html:
        text += f"\nScatter plot: {html}"
    text += f"\nCSV: {args.output}"
    print(text)
    if args.summary_file:
        os.makedirs(os.path.dirname(os.path.abspath(args.summary_file)) or ".", exist_ok=True)
        with open(args.summary_file, "w") as fh:
            fh.write(text + "\n")


if __name__ == "__main__":
    main()
