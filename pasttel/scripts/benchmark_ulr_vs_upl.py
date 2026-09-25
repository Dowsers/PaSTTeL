#!/usr/bin/env python3
"""Parse paired Ultimate logs (ULR vs UPL) into a CSV, and plot the comparison.

Each program is analysed twice by a full Ultimate run -- once with the stock
LassoRanker rank-synthesis backend (ULR), once with the PaSTTeL backend, which
falls back to LassoRanker per lasso whenever PaSTTeL does not conclude (UPL).
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
_PASTTEL_FALLBACK_RE = re.compile(r"falling back to LassoRanker")
_PASTTEL_INVOKE_FAIL_RE = re.compile(r"PaSTTeL invocation failed")
_PASTTEL_UNMAPPED_RE = re.compile(r"PaSTTeL reported \S+ but its certificate could not be mapped back")

COLUMNS = [
    "Program", "Ext", "Timeout (s)",
    "ULR Verdict", "ULR Wall (ms)", "ULR Plugin (ms)", "ULR Lassos (ms)", "ULR Iters",
    "UPL Verdict", "UPL Wall (ms)", "UPL Plugin (ms)", "UPL Lassos (ms)", "UPL Iters",
    "UPL PaSTTeL Calls", "UPL PaSTTeL Success", "UPL Fallbacks", "UPL Unmapped",
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
        "pasttel_success": 0, "pasttel_fallback": 0,
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
    out["pasttel_fallback"] = len(_PASTTEL_FALLBACK_RE.findall(text))
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
    if ulr_verdict == upl_verdict:
        return "SAME" if ulr_verdict in SOLVED else f"BOTH {ulr_verdict}"
    if upl_verdict in SOLVED and ulr_verdict not in SOLVED:
        return "UPL ONLY"
    if ulr_verdict in SOLVED and upl_verdict not in SOLVED:
        return "ULR ONLY"
    return "CONFLICT"


def build_rows(log_dir, timeout_s):
    rows = []
    for name in discover_programs(log_dir):
        parsed = {cfg: parse_log(os.path.join(log_dir, f"{name}.{cfg}.log"), timeout_s)
                  for cfg in CONFIGS}
        wall = {cfg: read_wall_ms(log_dir, name, cfg) for cfg in CONFIGS}
        u, p = parsed["ulr"], parsed["upl"]

        # A disagreement on a solved verdict is a soundness signal, not a
        # performance one -- surface it rather than averaging it away.
        conflict = (u["verdict"] in SOLVED and p["verdict"] in SOLVED
                    and u["verdict"] != p["verdict"])

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
            "UPL PaSTTeL Calls": p["pasttel_success"] + p["pasttel_fallback"],
            "UPL PaSTTeL Success": p["pasttel_success"],
            "UPL Fallbacks": p["pasttel_fallback"],
            "UPL Unmapped": p["pasttel_unmapped"],
            "UPL Techniques": "|".join(sorted(set(p["techniques"]))) or "-",
            "Agreement": "CONFLICT" if conflict else agreement(u["verdict"], p["verdict"]),
            "Speedup (wall)": fmt(speedup(wall["ulr"], wall["upl"]), 3),
            "Speedup (plugin)": fmt(speedup(u["plugin_ms"], p["plugin_ms"]), 3),
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


def aggregates(rows, col_key):
    """Totals and medians for one timing column, over the programs both sides solved.

    Totals are the headline number for "global resolution time": they answer how
    long the whole benchmark takes under each backend. They are restricted to
    commonly solved programs so that a timeout on one side cannot masquerade as
    time spent by the other.
    """
    cx, cy, label = COLUMN_FOR[col_key]
    pairs = []
    for r in rows:
        if r["ULR Verdict"] not in SOLVED or r["UPL Verdict"] not in SOLVED:
            continue
        x, y = parse_float(r[cx]), parse_float(r[cy])
        if x is not None and y is not None:
            pairs.append((x, y))
    if not pairs:
        return None
    ulr_total = sum(x for x, _ in pairs)
    upl_total = sum(y for _, y in pairs)
    ratios = [x / y for x, y in pairs if y > 0]
    return {
        "label": label, "n": len(pairs),
        "ulr_total_ms": ulr_total, "upl_total_ms": upl_total,
        "total_speedup": (ulr_total / upl_total) if upl_total > 0 else None,
        "median_speedup": statistics.median(ratios) if ratios else None,
        "min_speedup": min(ratios) if ratios else None,
        "max_speedup": max(ratios) if ratios else None,
        "upl_faster": sum(1 for x, y in pairs if y < x),
        "ulr_faster": sum(1 for x, y in pairs if x < y),
    }


def per_program_table(rows, col_key):
    """Text table: one line per program, ULR beside UPL for one timing column."""
    cx, cy, label = COLUMN_FOR[col_key]
    head = (f"{'Program':<48} {'ULR verdict':<15} {'UPL verdict':<15} "
            f"{'ULR (ms)':>11} {'UPL (ms)':>11} {'speedup':>9}  PaSTTeL")
    lines = [head, "-" * len(head)]
    for r in sorted(rows, key=lambda r: r["Program"]):
        x, y = parse_float(r[cx]), parse_float(r[cy])
        ratio = f"{x / y:.2f}x" if (x and y and y > 0) else "-"
        pastel = f"{r['UPL PaSTTeL Success']}/{r['UPL PaSTTeL Calls']}"
        if int(r["UPL Fallbacks"] or 0):
            pastel += f" (+{r['UPL Fallbacks']} fb)"
        lines.append(f"{r['Program'][:48]:<48} {r['ULR Verdict']:<15} {r['UPL Verdict']:<15} "
                     f"{fmt(x):>11} {fmt(y):>11} {ratio:>9}  {pastel}")
    return "\n".join(lines)


def render_summary(rows):
    """The whole summary as text, for stdout and for summary_tables.log alike."""
    out = []
    w = out.append
    conflicts = [r for r in rows if r["Agreement"] == "CONFLICT"]
    upl_only = [r for r in rows if r["Agreement"] == "UPL ONLY"]
    ulr_only = [r for r in rows if r["Agreement"] == "ULR ONLY"]
    unmapped = [r for r in rows if int(r["UPL Unmapped"] or 0) > 0]

    w("=" * 78)
    w(" TABLE 1 -- ULR vs UPL, per program (wall clock)")
    w("=" * 78)
    w(per_program_table(rows, "wall"))
    w("")
    w("=" * 78)
    w(" TABLE 2 -- verdicts")
    w("=" * 78)
    w(f" Programs                  : {len(rows)}")
    w(f" Solved by ULR             : {sum(1 for r in rows if r['ULR Verdict'] in SOLVED)}")
    w(f" Solved by UPL             : {sum(1 for r in rows if r['UPL Verdict'] in SOLVED)}")
    w(f" Solved only by UPL        : {len(upl_only)}")
    w(f" Solved only by ULR        : {len(ulr_only)}")
    w(f" Verdict conflicts         : {len(conflicts)}")
    w(f" PaSTTeL calls (total)     : {sum(int(r['UPL PaSTTeL Calls'] or 0) for r in rows)}")
    w(f"   conclusive              : {sum(int(r['UPL PaSTTeL Success'] or 0) for r in rows)}")
    w(f"   fell back to LassoRanker: {sum(int(r['UPL Fallbacks'] or 0) for r in rows)}")
    w(f"   certificate unmapped    : {sum(int(r['UPL Unmapped'] or 0) for r in rows)}")
    w("")
    w("=" * 78)
    w(" TABLE 3 -- global resolution time, over programs both sides solved")
    w("=" * 78)
    w(f" {'metric':<24} {'n':>4} {'ULR total':>13} {'UPL total':>13} {'total':>9} {'median':>9}"
      f" {'UPL faster':>11}")
    w(" " + "-" * 76)
    for key in ("wall", "plugin", "lassos"):
        a = aggregates(rows, key)
        if a is None:
            w(f" {COLUMN_FOR[key][2]:<24} {'-':>4}  (no commonly solved program)")
            continue
        w(f" {a['label']:<24} {a['n']:>4} {a['ulr_total_ms'] / 1000:>11.2f} s"
          f" {a['upl_total_ms'] / 1000:>11.2f} s {a['total_speedup']:>8.2f}x"
          f" {a['median_speedup']:>8.2f}x {a['upl_faster']:>6}/{a['n']:<4}")
    w("")
    w(" total  = sum(ULR) / sum(UPL); >1 means UPL resolves the benchmark faster overall.")
    w(" median = median of the per-program ratios, which weights small programs equally.")

    if conflicts:
        w("")
        w(" CONFLICTS (ULR and UPL disagree on a solved verdict):")
        for r in conflicts:
            w(f"   {r['Program']}: ULR={r['ULR Verdict']} UPL={r['UPL Verdict']}")
    if unmapped:
        w("")
        w(" Certificates PaSTTeL produced but could not map back:")
        for r in unmapped:
            w(f"   {r['Program']}: {r['UPL Unmapped']}")
    w("=" * 78)
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


AGREEMENT_COLOR = {
    "SAME": "#2e7d32",
    "UPL ONLY": "#1565c0",
    "ULR ONLY": "#ef6c00",
    "CONFLICT": "#c62828",
}


def _html_escape(text):
    return (str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def _summary_block_html(rows, col_key, ulr_label, upl_label):
    """Headline aggregates plus the per-program table, rendered beside the plot.

    The figure alone cannot say how much total time separates the two backends,
    nor which programs the points belong to; both questions come up immediately
    when reading a scatter, so the answers ship in the same file.
    """
    cx, cy, label = COLUMN_FOR[col_key]
    a = aggregates(rows, col_key)
    if a is None:
        cards = "<p>No program was solved by both configurations.</p>"
    else:
        def card(title, value, note):
            return (f'<div class="card"><div class="k">{_html_escape(title)}</div>'
                    f'<div class="v">{_html_escape(value)}</div>'
                    f'<div class="n">{_html_escape(note)}</div></div>')
        cards = (
            card("ULR total", f"{a['ulr_total_ms'] / 1000:.2f} s", f"{a['n']} programs")
            + card("UPL total", f"{a['upl_total_ms'] / 1000:.2f} s", f"{a['n']} programs")
            + card("Total speedup", f"{a['total_speedup']:.2f}x", "sum(ULR) / sum(UPL)")
            + card("Median speedup", f"{a['median_speedup']:.2f}x",
                   f"per program, {a['min_speedup']:.2f}x – {a['max_speedup']:.2f}x")
            + card("UPL faster on", f"{a['upl_faster']}/{a['n']}",
                   f"ULR faster on {a['ulr_faster']}"))

    body = []
    for r in sorted(rows, key=lambda r: r["Program"]):
        x, y = parse_float(r[cx]), parse_float(r[cy])
        ratio = f"{x / y:.2f}x" if (x and y and y > 0) else "—"
        cls = ""
        if x and y and y > 0:
            cls = "faster" if y < x else ("slower" if x < y else "")
        agree = r["Agreement"]
        acls = "bad" if agree == "CONFLICT" else ("good" if agree in ("SAME", "UPL ONLY") else "")
        body.append(
            f"<tr><td class='prog'>{_html_escape(r['Program'])}</td>"
            f"<td>{_html_escape(r['ULR Verdict'])}</td>"
            f"<td>{_html_escape(r['UPL Verdict'])}</td>"
            f"<td class='num'>{_html_escape(r[cx])}</td>"
            f"<td class='num'>{_html_escape(r[cy])}</td>"
            f"<td class='num {cls}'>{ratio}</td>"
            f"<td class='num'>{_html_escape(r['UPL PaSTTeL Success'])}/"
            f"{_html_escape(r['UPL PaSTTeL Calls'])}</td>"
            f"<td class='num'>{_html_escape(r['UPL Fallbacks'])}</td>"
            f"<td class='{acls}'>{_html_escape(agree)}</td></tr>")

    return f"""
<h1>{_html_escape(ulr_label)} vs {_html_escape(upl_label)} — {_html_escape(label)}</h1>
<p class="sub">Same Ultimate release, same toolchain; the two runs differ only in the
rank-synthesis backend. Totals cover the programs both configurations solved.</p>
<div class="cards">{cards}</div>
<table>
<thead><tr><th>Program</th><th>{_html_escape(ulr_label)}<br>verdict</th>
<th>{_html_escape(upl_label)}<br>verdict</th>
<th>{_html_escape(ulr_label)}<br>(ms)</th><th>{_html_escape(upl_label)}<br>(ms)</th>
<th>speedup</th><th>PaSTTeL ok/calls</th>
<th>fallbacks</th><th>agreement</th></tr></thead>
<tbody>{''.join(body)}</tbody>
</table>
"""


_CSS = """
:root { --fg:#1a1a1a; --muted:#666; --bd:#e0e0e0; --bg:#ffffff; --card:#f6f7f9;
        --good:#2e7d32; --bad:#c62828; }
@media (prefers-color-scheme: dark) {
  :root { --fg:#e8e8e8; --muted:#a0a0a0; --bd:#3a3a3a; --bg:#161616; --card:#212121;
          --good:#81c784; --bad:#ef9a9a; }
}
body { background:var(--bg); color:var(--fg); margin:0; padding:24px 16px;
       font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; }
.wrap { max-width:1100px; margin:0 auto; }
h1 { font-size:20px; margin:0 0 4px; }
.sub { color:var(--muted); margin:0 0 16px; max-width:70ch; }
.cards { display:flex; flex-wrap:wrap; gap:10px; margin-bottom:20px; }
.card { background:var(--card); border:1px solid var(--bd); border-radius:8px;
        padding:10px 14px; min-width:130px; }
.card .k { color:var(--muted); font-size:11px; text-transform:uppercase;
           letter-spacing:.04em; }
.card .v { font-size:20px; font-variant-numeric:tabular-nums; margin:2px 0; }
.card .n { color:var(--muted); font-size:11px; }
table { border-collapse:collapse; width:100%; margin-bottom:24px; font-size:13px; }
th,td { border-bottom:1px solid var(--bd); padding:6px 8px; text-align:left; }
th { color:var(--muted); font-weight:600; font-size:11px; text-transform:uppercase;
     letter-spacing:.04em; }
td.num { text-align:right; font-variant-numeric:tabular-nums; }
td.prog { font-family:ui-monospace,SFMono-Regular,Menlo,monospace; font-size:12px;
          word-break:break-all; }
.faster { color:var(--good); font-weight:600; }
.slower { color:var(--bad); }
.good { color:var(--good); }
.bad { color:var(--bad); font-weight:600; }
#plot { margin-bottom:16px; }
@media (max-width:640px) { body { padding:16px; } table { font-size:11px; } }
"""


def plot(csv_path, col_key, output_html, log_scale, timeout_s,
         ulr_label=DEFAULT_ULR_LABEL, upl_label=DEFAULT_UPL_LABEL):
    cx, cy, label = COLUMN_FOR[col_key]
    with open(csv_path, newline="") as fh:
        rows = list(csv.DictReader(fh))

    groups = {}
    for row in rows:
        x, y = parse_float(row.get(cx)), parse_float(row.get(cy))
        if x is None or y is None:
            continue
        key = row.get("Agreement", "SAME")
        key = key if key in AGREEMENT_COLOR else "SAME"
        groups.setdefault(key, {"x": [], "y": [], "t": []})
        groups[key]["x"].append(x)
        groups[key]["y"].append(y)
        groups[key]["t"].append(
            f"{row['Program']}<br>{ulr_label}: {row['ULR Verdict']}"
            f"<br>{upl_label}: {row['UPL Verdict']}"
            f"<br>PaSTTeL success/calls: {row['UPL PaSTTeL Success']}/{row['UPL PaSTTeL Calls']}"
            f"<br>techniques: {row['UPL Techniques']}")

    if not groups:
        print(f"No plottable data for '{col_key}' in {csv_path}", file=sys.stderr)
        return

    all_vals = [v for g in groups.values() for v in g["x"] + g["y"] if v > 0]
    lo, hi = (min(all_vals) * 0.5, max(all_vals) * 2.0) if all_vals else (1, 10)

    traces = []
    for key, g in groups.items():
        traces.append({
            "x": g["x"], "y": g["y"], "text": g["t"],
            "mode": "markers", "type": "scatter", "name": key,
            "hovertemplate": "%{text}<br>ULR: %{x:.1f} ms<br>UPL: %{y:.1f} ms<extra></extra>",
            "marker": {"size": 8, "color": AGREEMENT_COLOR[key],
                       "line": {"width": 0.5, "color": "#ffffff"}},
        })
    # Diagonal: below it UPL is faster than ULR.
    traces.append({
        "x": [lo, hi], "y": [lo, hi], "mode": "lines", "type": "scatter",
        "name": "x = y", "hoverinfo": "skip",
        "line": {"dash": "dash", "width": 1, "color": "#888888"},
    })

    axis = {"type": "log" if log_scale else "linear", "range":
            ([__import__("math").log10(lo), __import__("math").log10(hi)] if log_scale else [lo, hi])}
    layout = {
        "title": f"{ulr_label}  vs  {upl_label} — {label}"
                 f"<br><sub>points below the diagonal: UPL faster</sub>",
        "xaxis": dict(axis, title=f"{ulr_label} — {label} (ms)"),
        "yaxis": dict(axis, title=f"{upl_label} — {label} (ms)"),
        "hovermode": "closest", "height": 700, "autosize": True,
        "legend": {"title": {"text": "verdict agreement"}},
    }

    import json
    html = f"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{ulr_label} vs {upl_label} — {label}</title>
<style>{_CSS}</style>
{_plotly_script_tag()}
</head><body>
<div class="wrap">
{_summary_block_html(rows, col_key, ulr_label, upl_label)}
<div id="plot"></div>
</div>
<script>
const layout = {json.dumps(layout)};
layout.paper_bgcolor = "rgba(0,0,0,0)";
layout.plot_bgcolor = "rgba(0,0,0,0)";
if (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches) {{
  layout.font = {{color: "#e8e8e8"}};
}}
Plotly.newPlot("plot", {json.dumps(traces)}, layout, {{responsive: true}});
</script>
</body></html>"""
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
