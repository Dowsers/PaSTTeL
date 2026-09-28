#!/usr/bin/env python3
"""Parse paired Ultimate logs (ULR vs UPL) into a CSV, and plot the comparison.

Each program is analysed twice by a full Ultimate run -- once with the stock
LassoRanker rank-synthesis backend (ULR), once with the PaSTTeL backend for
ranking functions, without any LassoRanker fallback (UPL).
scripts/run_ulr_vs_upl.sh produces, per program and per configuration:

    <name>.<cfg>.log       Ultimate stdout+stderr
    <name>.<cfg>.wall_ms   wall clock measured around the process, in ms

This module turns those into one row per program. Three timings are recorded
side by side because they answer different questions: wall clock includes JVM
startup and parsing, the plugin time isolates BuchiAutomizer, and the lasso
analysis time isolates the part PaSTTeL actually replaces.
"""

import argparse
import csv
import glob
import os
import re
import statistics
import sys

CONFIGS = ("ulr", "upl")

# Axis and column labels. A scatter of two Ultimate runs is unreadable unless it
# says which backend each axis is: both runs are "Ultimate", and the only
# difference lives in the settings file. run_ulr_vs_upl.sh overrides these with
# the .epf names it actually used.
DEFAULT_ULR_LABEL = "ULR — LassoRanker backend"
DEFAULT_UPL_LABEL = "UPL — PaSTTeL backend"

# --- Verdicts ---------------------------------------------------------------
# TerminationAnalysisResult.getShortDescription() in the Ultimate sources.
_VERDICT_PATTERNS = (
    ("TERMINATING", re.compile(r"TerminationAnalysisResult:\s*Termination proven")),
    ("NONTERMINATING", re.compile(r"TerminationAnalysisResult:\s*Nontermination possible")),
    ("UNKNOWN", re.compile(r"TerminationAnalysisResult:\s*Unable to decide termination")),
)
_ERROR_RE = re.compile(r"ExceptionOrErrorResult")

# --- Timings ----------------------------------------------------------------
# Ultimate prints decimal separators according to the JVM locale, so a French
# locale yields "1376,85ms" where an English one yields "1376.85ms". Both are
# accepted everywhere a number is read.
_NUM = r"(\d+(?:[.,]\d+)?)"
_PLUGIN_RE = re.compile(r"BüchiAutomizer plugin needed " + _NUM + r"\s*s and (\d+) iterations")
_PLUGIN_ASCII_RE = re.compile(r"B\S*chiAutomizer plugin needed " + _NUM + r"\s*s and (\d+) iterations")
_LASSOS_RE = re.compile(r"Analysis of lassos took " + _NUM + r"\s*s")
_TOOLCHAIN_RE = re.compile(r"BuchiAutomizer took " + _NUM + r"\s*ms")

# --- PaSTTeL backend activity (UPL logs only) -------------------------------
# All emitted by LassoCheck; see BuchiAutomizer/.../LassoCheck.java.
# Termination logs "SUCCESS via <technique>, ranking function <rf>", while
# non-termination stops after the technique -- so stop at a comma, not at
# whitespace, or the technique keeps the separator.
_PASTTEL_SUCCESS_RE = re.compile(r"PaSTTeL (?:termination|non-termination) check: SUCCESS via ([^,\n]+)")
# A PaSTTeL call that produced no argument (UNKNOWN, timeout, crash, unmapped certificate). Since UPL has no
# LassoRanker fallback, LassoCheck logs "no ranking function"; logs of older builds say "falling back to
# LassoRanker" for the same event.
_PASTTEL_NO_RESULT_RE = re.compile(r"PaSTTeL .*(?:no ranking function|falling back to LassoRanker)")
_PASTTEL_INVOKE_FAIL_RE = re.compile(r"PaSTTeL invocation failed")
_PASTTEL_UNMAPPED_RE = re.compile(r"PaSTTeL reported \S+ but its certificate could not be mapped back")

COLUMNS = [
    "Program", "Ext", "Timeout (s)",
    "ULR Verdict", "ULR Wall (ms)", "ULR Plugin (ms)", "ULR Lassos (ms)", "ULR Iters",
    "UPL Verdict", "UPL Wall (ms)", "UPL Plugin (ms)", "UPL Lassos (ms)", "UPL Iters",
    "UPL PaSTTeL Calls", "UPL PaSTTeL Success", "UPL No Result", "UPL Unmapped",
    "UPL Techniques",
    "Agreement", "Speedup (wall)", "Speedup (plugin)",
]

SOLVED = ("TERMINATING", "NONTERMINATING")
MISSING = "NO LOG"


def _num(text):
    """Parse a number written with either decimal separator."""
    return float(text.replace(",", "."))


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
        # Distinguish a crashed run from one the timeout killed: only the
        # latter is a legitimate data point for a timeout-bounded comparison.
        out["verdict"] = "ERROR" if _ERROR_RE.search(text) else "TIMEOUT"

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
    """How many times faster UPL is than ULR. None when either side is missing."""
    if ulr is None or upl is None or upl <= 0:
        return None
    return ulr / upl


def agreement(ulr_verdict, upl_verdict):
    # A side that was not run at all (--skip-ulr / --skip-upl) is missing data,
    # not a disagreement: saying otherwise would report every row of a one-sided
    # run as a verdict conflict.
    if ulr_verdict == MISSING and upl_verdict == MISSING:
        return "NO DATA"
    if ulr_verdict == MISSING:
        return "ULR MISSING"
    if upl_verdict == MISSING:
        return "UPL MISSING"
    u_ok, p_ok = ulr_verdict in SOLVED, upl_verdict in SOLVED
    if u_ok and p_ok:
        # The only disagreement that is a conflict: both concluded, with opposite verdicts.
        return "SAME" if ulr_verdict == upl_verdict else "CONFLICT"
    if p_ok:
        return "UPL ONLY"
    if u_ok:
        return "ULR ONLY"
    # Neither solved: a TIMEOUT against an ERROR is two failures, not a conflict.
    return f"BOTH {ulr_verdict}" if ulr_verdict == upl_verdict else "NEITHER"


def build_rows(log_dir, timeout_s):
    rows = []
    for name in discover_programs(log_dir):
        parsed = {cfg: parse_log(os.path.join(log_dir, f"{name}.{cfg}.log"), timeout_s)
                  for cfg in CONFIGS}
        wall = {cfg: read_wall_ms(log_dir, name, cfg) for cfg in CONFIGS}
        u, p = parsed["ulr"], parsed["upl"]

        # A speedup only means something when both runs solved the program: a run that crashed or
        # gave up early is not faster, it did not answer.
        both_solved = u["verdict"] in SOLVED and p["verdict"] in SOLVED

        rows.append({
            "Program": name,
            "Ext": name.rsplit(".", 1)[-1] if "." in name else "-",
            "Timeout (s)": timeout_s,
            "ULR Verdict": u["verdict"],
            "ULR Wall (ms)": fmt(wall["ulr"]),
            "ULR Plugin (ms)": fmt(u["plugin_ms"]),
            "ULR Lassos (ms)": fmt(u["lassos_ms"]),
            "ULR Iters": u["iterations"] if u["iterations"] is not None else "-",
            "UPL Verdict": p["verdict"],
            "UPL Wall (ms)": fmt(wall["upl"]),
            "UPL Plugin (ms)": fmt(p["plugin_ms"]),
            "UPL Lassos (ms)": fmt(p["lassos_ms"]),
            "UPL Iters": p["iterations"] if p["iterations"] is not None else "-",
            "UPL PaSTTeL Calls": p["pasttel_success"] + p["pasttel_no_result"],
            "UPL PaSTTeL Success": p["pasttel_success"],
            "UPL No Result": p["pasttel_no_result"],
            "UPL Unmapped": p["pasttel_unmapped"],
            "UPL Techniques": "|".join(sorted(set(p["techniques"]))) or "-",
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
    print(f"CSV written to: {path}  ({len(rows)} program(s))")


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
    "wall": ("ULR Wall (ms)", "UPL Wall (ms)", "wall clock"),
    "plugin": ("ULR Plugin (ms)", "UPL Plugin (ms)", "BuchiAutomizer plugin"),
    "lassos": ("ULR Lassos (ms)", "UPL Lassos (ms)", "lasso analysis"),
}

# One classification feeds the text summary, the HTML table and the scatter plot alike, so that the
# three can never tell different stories. Only a TERMINATING or NONTERMINATING verdict counts as
# solved: a TIMEOUT, UNKNOWN or ERROR run solved nothing, however fast it ended, and is placed at the
# PAR-2 penalty (twice the Ultimate timeout) instead of its own time.
#   key, label, colour, marker, plotted
CATEGORIES = (
    ("term", "Terminating (common)", "green", "circle", True),
    ("nonterm", "Non-terminating (common)", "blue", "circle", True),
    ("contra", "Contradiction (both disagree)", "black", "star", True),
    ("ulr_only", "Solved by ULR only (UPL: PAR-2)", "orange", "circle-open", True),
    ("upl_only", "Solved by UPL only (ULR: PAR-2)", "purple", "diamond-open", True),
    # both at PAR-2: every such program would sit on the same corner point, so it is only counted
    ("neither", "Solved by neither (PAR-2 both)", "red", "x", False),
)


def _as_int(value):
    try:
        return int(str(value).strip())
    except ValueError:
        return 0


def categorize(rows, col_key, default_timeout_s):
    """Sort programs into CATEGORIES for one timing column.

    Returns ({key: [point]}, excluded) where a point is (program, x_ms, y_ms, row), x for ULR and y
    for UPL, and excluded counts the programs left out: both runs ended in ERROR (they say nothing
    about either backend), or one side was never run (--skip-ulr / --skip-upl).
    """
    cx, cy, _ = COLUMN_FOR[col_key]
    cats = {key: [] for key, *_ in CATEGORIES}
    excluded = {"both ERROR": 0, "one side not run": 0}
    for r in rows:
        u, p = r["ULR Verdict"], r["UPL Verdict"]
        if MISSING in (u, p):
            excluded["one side not run"] += 1
            continue
        if u == "ERROR" and p == "ERROR":
            excluded["both ERROR"] += 1
            continue
        par2 = 2 * 1000.0 * (parse_float(r.get("Timeout (s)")) or default_timeout_s)
        u_ok, p_ok = u in SOLVED, p in SOLVED
        # A solved side without a parsed time (e.g. missing statistics line) keeps PAR-2 rather than
        # vanishing from the totals.
        x = (parse_float(r[cx]) if u_ok else None) or par2
        y = (parse_float(r[cy]) if p_ok else None) or par2
        if u_ok and p_ok:
            key = "contra" if u != p else ("term" if u == "TERMINATING" else "nonterm")
        elif u_ok:
            key = "ulr_only"
        elif p_ok:
            key = "upl_only"
        else:
            key = "neither"
        cats[key].append((r["Program"], x, y, r))
    return cats, excluded


def summary_rows(cats):
    """(label, count, ULR total s, UPL total s) per category, then the two cumulative lines."""
    out = []
    for key, label, *_ in CATEGORIES:
        pts = cats[key]
        out.append((key, label, len(pts), sum(x for _, x, _, _ in pts) / 1000, sum(y for _, _, y, _ in pts) / 1000))
    both = cats["term"] + cats["nonterm"]
    # Programs neither run solved are left out of the PAR-2 total: they would add the same 2 x timeout
    # to both sides and only dilute the difference between the two backends.
    solved = [pt for key, *_ in CATEGORIES if key != "neither" for pt in cats[key]]
    out.append(("both", "Solved by both (cumulative)", len(both),
                sum(x for _, x, _, _ in both) / 1000, sum(y for _, _, y, _ in both) / 1000))
    out.append(("par2", "PAR-2 total (solved by at least one)", len(solved),
                sum(x for _, x, _, _ in solved) / 1000, sum(y for _, _, y, _ in solved) / 1000))
    return out


def summary_table_text(rows, col_key, default_timeout_s, ulr_label, upl_label):
    cats, excluded = categorize(rows, col_key, default_timeout_s)
    metric = COLUMN_FOR[col_key][2]
    xh, yh = f"{ulr_label} (s)", f"{upl_label} (s)"
    w = max(len(xh), len(yh), 14)
    hdr = f"{'Category':<36}  {'Count':>6}  {xh:>{w}}  {yh:>{w}}"
    sep = "-" * len(hdr)
    lines = [f"=== {metric} ===", sep, hdr, sep]
    for key, label, n, tx, ty in summary_rows(cats):
        if key == "both":
            lines.append(sep)
        lines.append(f"{label:<36}  {n:>6}  {tx:>{w}.2f}  {ty:>{w}.2f}")
    lines.append(sep)
    left_out = [f"{n} {why}" for why, n in excluded.items() if n]
    if left_out:
        lines.append(f"Not shown: {', '.join(left_out)}.")
    return "\n".join(lines), cats


def render_summary(rows):
    """The whole text summary, for stdout and summary_tables.log alike."""
    labels = (DEFAULT_ULR_LABEL, DEFAULT_UPL_LABEL)
    timeout = next((parse_float(r.get("Timeout (s)")) for r in rows if parse_float(r.get("Timeout (s)"))), 0)
    out = [f"ULR vs UPL -- {len(rows)} program(s), PAR-2 penalty = 2 x {timeout:g} s", ""]
    # Wall clock only: it is the time a user of Ultimate actually waits. The plugin and lasso-analysis
    # columns stay in the CSV, and --plot --col plugin|lassos still draws them.
    text, cats = summary_table_text(rows, "wall", timeout, *labels)
    out += [text, ""]
    calls = sum(_as_int(r["UPL PaSTTeL Calls"]) for r in rows)
    ok = sum(_as_int(r["UPL PaSTTeL Success"]) for r in rows)
    nores = sum(_as_int(r["UPL No Result"]) for r in rows)
    unm = sum(_as_int(r["UPL Unmapped"]) for r in rows)
    out.append(f"PaSTTeL inside UPL: {calls} call(s), {ok} conclusive, {nores} without result, "
               f"{unm} certificate(s) not mapped back.")
    # A contradiction is a soundness signal: name the programs rather than only counting them.
    if cats and cats["contra"]:
        out.append("")
        out.append("CONTRADICTIONS (both runs solved the program, with opposite verdicts):")
        for prog, _, _, r in cats["contra"]:
            out.append(f"  {prog}: ULR={r['ULR Verdict']} UPL={r['UPL Verdict']}")
    return "\n".join(out)


def print_summary(rows, summary_file=None):
    text = render_summary(rows)
    print()
    print(text)
    if summary_file:
        os.makedirs(os.path.dirname(os.path.abspath(summary_file)) or ".", exist_ok=True)
        with open(summary_file, "w") as fh:
            fh.write(text + "\n")
        print(f"\nSummary written to: {summary_file}")


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


def plot(csv_path, col_key, output_html, log_scale, timeout_s,
         ulr_label=DEFAULT_ULR_LABEL, upl_label=DEFAULT_UPL_LABEL):
    """Scatter plot plus summary table, in the style of the P-ULR plots
    (benchmark_ultimate_vs_pasttel.py): white page, one table, one figure."""
    import json
    with open(csv_path, newline="") as fh:
        rows = list(csv.DictReader(fh))
    cats, _ = categorize(rows, col_key, timeout_s)
    metric = COLUMN_FOR[col_key][2]
    par2_s = 2 * (next((parse_float(r.get("Timeout (s)")) for r in rows if parse_float(r.get("Timeout (s)"))), 0)
                  or timeout_s)
    title = f"{ulr_label} vs {upl_label}"

    traces, pts_all = [], []
    for key, label, colour, symbol, plotted in CATEGORIES:
        pts = cats[key]
        if not plotted or not pts:
            continue
        pts_all += pts
        traces.append({
            "x": [x for _, x, _, _ in pts], "y": [y for _, _, y, _ in pts],
            "text": [f"{prog}<br>ULR: {r['ULR Verdict']}, {x:.1f} ms<br>UPL: {r['UPL Verdict']}, {y:.1f} ms"
                     f"<br>PaSTTeL conclusive/calls: {r['UPL PaSTTeL Success']}/{r['UPL PaSTTeL Calls']}"
                     for prog, x, y, r in pts],
            "mode": "markers", "type": "scatter", "name": f"{label} ({len(pts)})", "hoverinfo": "text",
            "marker": {"color": colour, "symbol": symbol, "opacity": 0.8,
                       "size": 12 if symbol == "star" else 9},
        })
    if not pts_all:
        print(f"No plottable data for '{col_key}' in {csv_path}", file=sys.stderr)
        return
    vals = [v for _, x, y, _ in pts_all for v in (x, y) if v > 0]
    lo, hi = min(vals) * 0.8, max(vals) * 1.2
    # Diagonal from a positive start, so that it is drawn on logarithmic axes too.
    traces.insert(0, {"x": [lo, hi], "y": [lo, hi], "mode": "lines", "type": "scatter", "name": "y = x",
                      "line": {"color": "gray", "width": 1.5, "dash": "dash"}, "hoverinfo": "skip"})
    axis_type = {"type": "log"} if log_scale else {"rangemode": "tozero"}
    layout = {
        "xaxis": dict(axis_type, title=f"{ulr_label} — {metric} (ms)"),
        "yaxis": dict(axis_type, title=f"{upl_label} — {metric} (ms)"),
        "hovermode": "closest",
        "legend": {"x": 0.01, "y": 0.99, "bgcolor": "rgba(255,255,255,0.8)"},
        "margin": {"l": 70, "r": 30, "t": 30, "b": 70},
    }

    style = {"term": "color:green;", "nonterm": "color:blue;", "contra": "color:black; background:#fff3cd;",
             "ulr_only": "color:orange;", "upl_only": "color:purple;", "neither": "color:red;",
             "both": "font-weight:bold; border-top:2px solid #333;", "par2": "font-weight:bold;"}
    body = "".join(
        f'  <tr style="{style[key]}"><td>{_esc(label)}</td><td style="text-align:right;">{n}</td>'
        f'<td style="text-align:right;">{tx:.2f}</td><td style="text-align:right;">{ty:.2f}</td></tr>\n'
        for key, label, n, tx, ty in summary_rows(cats))
    summary_html = f"""<h3>Summary</h3>
<table border="1" cellpadding="6" cellspacing="0"
       style="border-collapse:collapse; font-family:monospace; margin-bottom:20px;">
<thead style="background:#f0f0f0;">
  <tr><th>Category</th><th>Count</th><th>{_esc(ulr_label)} total (s)</th><th>{_esc(upl_label)} total (s)</th></tr>
</thead>
<tbody>
{body}</tbody>
</table>"""

    legend = " &nbsp;\n  ".join(
        f'<span style="color:{colour};">{"&#9733;" if symbol == "star" else "&#9679;"}</span> {_esc(label)}'
        for _, label, colour, symbol, plotted in CATEGORIES if plotted)
    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{_esc(title)} - Scatter Plot</title>
{_plotly_script_tag()}
<style>
  body {{ font-family: Arial, sans-serif; margin: 20px; }}
  #plot {{ width: 100%; height: 85vh; }}
</style>
</head>
<body>
<h2>{_esc(title)} &mdash; {_esc(metric)}</h2>
<p>
  {legend}
</p>
<p style="font-size:0.85em; color:#555;">
  Each point is one program, analysed twice by the same Ultimate release, once per rank-synthesis
  backend. Only TERMINATING and NONTERMINATING count as solved: a side that timed out, answered
  UNKNOWN or stopped with an ERROR is placed at the PAR-2 penalty, twice the timeout ({par2_s:g} s).
  Programs where both runs stopped with an ERROR are not shown.
</p>
{summary_html}
<div id="plot"></div>
<script>
Plotly.newPlot('plot', {json.dumps(traces)}, {json.dumps(layout)});
</script>
</body>
</html>"""
    os.makedirs(os.path.dirname(os.path.abspath(output_html)) or ".", exist_ok=True)
    with open(output_html, "w") as fh:
        fh.write(html)
    print(f"Plot written to: {output_html}")


def main():
    ap = argparse.ArgumentParser(description="ULR vs UPL: parse Ultimate logs, emit CSV and plots")
    ap.add_argument("--log-dir", help="Directory holding <prog>.{ulr,upl}.log files")
    ap.add_argument("--output", default="results_ULR_vs_UPL.csv", help="Output CSV (or HTML with --plot)")
    ap.add_argument("--timeout", type=int, default=600, help="Ultimate timeout used, in seconds")
    ap.add_argument("--summary", action="store_true", help="Print the summary tables")
    ap.add_argument("--summary-file", default=None,
                    help="Also write the summary tables to this file")
    ap.add_argument("--plot", metavar="CSV", help="Plot an existing CSV instead of parsing logs")
    ap.add_argument("--col", choices=sorted(COLUMN_FOR), default="wall", help="Timing column to plot")
    ap.add_argument("--log-scale", action="store_true", help="Logarithmic axes")
    ap.add_argument("--ulr-label", default=DEFAULT_ULR_LABEL,
                    help="Name of the baseline configuration, used on the x axis")
    ap.add_argument("--upl-label", default=DEFAULT_UPL_LABEL,
                    help="Name of the PaSTTeL configuration, used on the y axis")
    args = ap.parse_args()

    if args.plot:
        if not os.path.isfile(args.plot):
            ap.error(f"CSV not found: {args.plot}")
        out = args.output
        if out.endswith(".csv"):
            out = out[:-4] + f"_{args.col}.html"
        plot(args.plot, args.col, out, args.log_scale, args.timeout,
             args.ulr_label, args.upl_label)
        return

    if not args.log_dir:
        ap.error("--log-dir is required unless --plot is given")
    if not os.path.isdir(args.log_dir):
        ap.error(f"log directory not found: {args.log_dir}")

    rows = build_rows(args.log_dir, args.timeout)
    if not rows:
        print(f"No <prog>.{{ulr,upl}}.log found in {args.log_dir}", file=sys.stderr)
        sys.exit(1)
    write_csv(rows, args.output)
    if args.summary or args.summary_file:
        print_summary(rows, args.summary_file)


if __name__ == "__main__":
    main()
