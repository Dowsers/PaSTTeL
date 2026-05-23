#!/usr/bin/env python3
"""
Compare two PaSTTeL benchmark CSV files on a chosen time column.

Typical use: compare Z3 vs CVC5 runs on the same benchmark set.

Usage:
    python3 scripts/compare_csv.py \\
        --csv-x results_z3.csv \\
        --csv-y results_cvc5.csv \\
        --col "pasttel (ms)" \\
        --label-x "Z3" --label-y "CVC5" \\
        --timeout 700 \\
        --output compare_z3_vs_cvc5.html

The two CSVs must share the same "Trace Name" column.  Rows present in only
one file are skipped.  Rows with Result Code INFEASIBLE/UNCHECKED are skipped.

Columns that must exist (default: "pasttel (ms)"):
    --col      time column used for BOTH axes (e.g. "pasttel (ms)")
    --col-x    override time column for X axis only
    --col-y    override time column for Y axis only

The "Result Code" column is used to colour points:
    green  = TERMINATING in both
    blue   = NONTERMINATING in both
    orange = timeout on Y axis (time missing / PAR-2 applied)
    red    = disagreement between the two files
"""

import argparse
import csv
import json
import os
import sys


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _load_csv(path: str) -> dict[str, dict]:
    """Return {trace_name: row_dict} for every row in the CSV."""
    rows = {}
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = row["Trace Name"].strip()
            rows[key] = row
    return rows


def _parse_float(s: str) -> float | None:
    """Parse a time value; return None when absent/unknown."""
    if s is None:
        return None
    s = s.strip().strip('"').replace(",", ".")
    if s in ("-", "", "N/A", "nan"):
        return None
    try:
        return float(s)
    except ValueError:
        return None


def _verdict(row: dict) -> str:
    """Return TERMINATING / NONTERMINATING / UNKNOWN from a CSV row."""
    # Prefer explicit PaSTTeL Status column
    status = row.get("PaSTTeL Status", "").strip()
    if status in ("TERMINATING", "NONTERMINATING"):
        return status
    code = row.get("PaSTTeL Status", "").strip()
    if code in ("TERMINATING", "NONTERMINATING"):
        return code
    return "UNKNOWN"


def _plotly_script_tag() -> str:
    try:
        import plotly as _pl
        _js = os.path.join(os.path.dirname(_pl.__file__), "package_data", "plotly.min.js")
        with open(_js) as _f:
            return f"<script>{_f.read()}</script>"
    except Exception:
        return '<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>'


# ---------------------------------------------------------------------------
# Main comparison / plot
# ---------------------------------------------------------------------------

def compare_and_plot(
    csv_x: str,
    csv_y: str,
    col_x: str,
    col_y: str,
    label_x: str,
    label_y: str,
    timeout_s: float,
    output_html: str,
    log_scale: bool,
) -> None:
    par2_ms = timeout_s * 2 * 1000.0

    rows_x = _load_csv(csv_x)
    rows_y = _load_csv(csv_y)

    common_keys = sorted(set(rows_x) & set(rows_y))
    if not common_keys:
        print("No common traces between the two CSV files.", file=sys.stderr)
        sys.exit(1)

    # Colour buckets
    green_x,  green_y,  green_labels  = [], [], []
    blue_x,   blue_y,   blue_labels   = [], [], []
    orange_x, orange_y, orange_labels = [], [], []
    red_x,    red_y,    red_labels    = [], [], []

    n_skipped = 0

    for name in common_keys:
        rx = rows_x[name]
        ry = rows_y[name]

        # Skip rows with no meaningful result
        code = rx.get("Result Code", "").strip()
        px = rx.get("PaSTTeL Status", "").strip()
        py = ry.get("PaSTTeL Status", "").strip()
        if (code in ("INFEASIBLE", "UNCHECKED", "NOT_SUPPORTED")
                or px == "NOT_SUPPORTED" or py == "NOT_SUPPORTED"):
            n_skipped += 1
            continue

        vx = _verdict(rx)
        vy = _verdict(ry)

        tx_raw = _parse_float(rx.get(col_x, "-"))
        ty_raw = _parse_float(ry.get(col_y, "-"))

        # Need at least the X value to plot
        if tx_raw is None:
            n_skipped += 1
            continue

        tx = tx_raw
        ty = ty_raw if ty_raw is not None else par2_ms
        timeout_y = ty_raw is None

        algo_x = rx.get("Algo", "").strip()
        algo_y = ry.get("Algo", "").strip()
        tip = (
            f"{name}<br>"
            f"{label_x}: {tx:.2f} ms  ({vx})<br>"
            f"{label_y}: {ty:.2f} ms  ({vy})"
        )
        if algo_x:
            tip += f"<br>Algo-X: {algo_x}"
        if algo_y and algo_y != algo_x:
            tip += f"<br>Algo-Y: {algo_y}"

        if timeout_y:
            orange_x.append(tx)
            orange_y.append(ty)
            orange_labels.append(tip + f"<br>{label_y}: TIMEOUT (PAR-2={par2_ms:.0f} ms)")
        elif vx == "TERMINATING" and vy == "TERMINATING":
            green_x.append(tx)
            green_y.append(ty)
            green_labels.append(tip)
        elif vx == "NONTERMINATING" and vy == "NONTERMINATING":
            blue_x.append(tx)
            blue_y.append(ty)
            blue_labels.append(tip)
        elif vx == "UNKNOWN" or vy == "UNKNOWN":
            # One or both solvers gave no verdict (timeout with time recorded,
            # or genuinely unknown) — treat as orange rather than disagreement
            orange_x.append(tx)
            orange_y.append(ty)
            orange_labels.append(tip + f"<br>No verdict: {label_x}={vx}, {label_y}={vy}")
        else:
            # Both have a verdict but they differ (genuine disagreement)
            red_x.append(tx)
            red_y.append(ty)
            red_labels.append(tip + f"<br>DISAGREEMENT: {label_x}={vx}, {label_y}={vy}")

    all_x = green_x + blue_x + orange_x + red_x
    all_y = green_y + blue_y + orange_y + red_y

    if not all_x:
        print("No plottable data points found.", file=sys.stderr)
        sys.exit(1)

    # ── Summary ──────────────────────────────────────────────────────────────
    n_term    = len(green_x)
    n_nonterm = len(blue_x)
    n_timeout = len(orange_x)
    n_disag   = len(red_x)

    total_tx_term    = sum(green_x)  / 1000.0
    total_ty_term    = sum(green_y)  / 1000.0
    total_tx_nonterm = sum(blue_x)   / 1000.0
    total_ty_nonterm = sum(blue_y)   / 1000.0

    hdr  = f"{'Category':<28}  {'Count':>6}  {label_x+' total (s)':>20}  {label_y+' total (s)':>20}"
    sep  = "-" * len(hdr)
    rows_txt = [
        f"{'Terminating (both agree)':<28}  {n_term:>6}  {total_tx_term:>17.2f} s  {total_ty_term:>17.2f} s",
        f"{'Non-terminating (both agree)':<28}  {n_nonterm:>6}  {total_tx_nonterm:>17.2f} s  {total_ty_nonterm:>17.2f} s",
        f"{'Timeout '+label_y:<28}  {n_timeout:>6}  {'—':>20}  {'—':>20}",
        f"{'Disagreement':<28}  {n_disag:>6}  {'—':>20}  {'—':>20}",
        f"{'Skipped (INFEASIBLE…)':<28}  {n_skipped:>6}  {'—':>20}  {'—':>20}",
    ]
    print(f"\n{sep}\n{hdr}\n{sep}")
    for r in rows_txt:
        print(r)
    print(sep + "\n")

    summary_html = f"""
<h3>Summary</h3>
<table border="1" cellpadding="6" cellspacing="0"
       style="border-collapse:collapse; font-family:monospace; margin-bottom:20px;">
<thead style="background:#f0f0f0;">
  <tr>
    <th>Category</th><th>Count</th>
    <th>{label_x} total (s)</th>
    <th>{label_y} total (s)</th>
  </tr>
</thead>
<tbody>
  <tr style="color:green;">
    <td>Terminating (both agree)</td>
    <td style="text-align:right;">{n_term}</td>
    <td style="text-align:right;">{total_tx_term:.2f}</td>
    <td style="text-align:right;">{total_ty_term:.2f}</td>
  </tr>
  <tr style="color:blue;">
    <td>Non-terminating (both agree)</td>
    <td style="text-align:right;">{n_nonterm}</td>
    <td style="text-align:right;">{total_tx_nonterm:.2f}</td>
    <td style="text-align:right;">{total_ty_nonterm:.2f}</td>
  </tr>
  <tr style="color:orange;">
    <td>Timeout {label_y}</td>
    <td style="text-align:right;">{n_timeout}</td>
    <td style="text-align:right;">—</td><td style="text-align:right;">—</td>
  </tr>
  <tr style="color:red;">
    <td>Disagreement</td>
    <td style="text-align:right;">{n_disag}</td>
    <td style="text-align:right;">—</td><td style="text-align:right;">—</td>
  </tr>
  <tr>
    <td>Skipped (INFEASIBLE / no time)</td>
    <td style="text-align:right;">{n_skipped}</td>
    <td style="text-align:right;">—</td><td style="text-align:right;">—</td>
  </tr>
</tbody>
</table>"""

    # ── Plotly HTML ───────────────────────────────────────────────────────────
    max_val = max(max(all_x), max(all_y))
    plot_title = f"{label_x} vs {label_y}"

    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{plot_title} - Scatter Plot</title>
{_plotly_script_tag()}
<style>
  body {{ font-family: Arial, sans-serif; margin: 20px; }}
  #plot {{ width: 100%; height: 85vh; }}
</style>
</head>
<body>
<h2>{plot_title} &mdash; Computation Time Comparison</h2>
<p>
  <span style="color:green;">&#9679;</span> Terminating (both agree) &nbsp;
  <span style="color:blue;">&#9679;</span> Non-terminating (both agree) &nbsp;
  <span style="color:orange;">&#9679;</span> Timeout / no verdict &nbsp;
  <span style="color:red;">&#9679;</span> Disagreement (both have verdict but differ)
</p>
{summary_html}
<div id="plot"></div>
<script>
var green = {{
  x: {json.dumps(green_x)},
  y: {json.dumps(green_y)},
  text: {json.dumps(green_labels)},
  mode: 'markers', type: 'scatter',
  name: 'Terminating ({n_term})',
  marker: {{ color: 'green', size: 8, opacity: 0.7 }},
  hoverinfo: 'text'
}};
var blue = {{
  x: {json.dumps(blue_x)},
  y: {json.dumps(blue_y)},
  text: {json.dumps(blue_labels)},
  mode: 'markers', type: 'scatter',
  name: 'Non-terminating ({n_nonterm})',
  marker: {{ color: 'blue', size: 8, opacity: 0.7 }},
  hoverinfo: 'text'
}};
var orange = {{
  x: {json.dumps(orange_x)},
  y: {json.dumps(orange_y)},
  text: {json.dumps(orange_labels)},
  mode: 'markers', type: 'scatter',
  name: 'Timeout / no verdict ({n_timeout})',
  marker: {{ color: 'orange', size: 9, opacity: 0.85, symbol: 'circle-open' }},
  hoverinfo: 'text'
}};
var red = {{
  x: {json.dumps(red_x)},
  y: {json.dumps(red_y)},
  text: {json.dumps(red_labels)},
  mode: 'markers', type: 'scatter',
  name: 'Disagreement ({n_disag})',
  marker: {{ color: 'red', size: 10, opacity: 0.85, symbol: 'x' }},
  hoverinfo: 'text'
}};
var diag_max = {max_val * 1.05};
var diagonal = {{
  x: [0, diag_max], y: [0, diag_max],
  mode: 'lines', type: 'scatter',
  name: 'y = x',
  line: {{ color: 'gray', width: 1.5, dash: 'dash' }},
  hoverinfo: 'skip', showlegend: true
}};
var layout = {{
  xaxis: {{
    title: '{label_x} — {col_x} (ms)',
    {"type: 'log'," if log_scale else "rangemode: 'tozero',"}
  }},
  yaxis: {{
    title: '{label_y} — {col_y} (ms)',
    {"type: 'log'," if log_scale else "rangemode: 'tozero',"}
  }},
  hovermode: 'closest',
  legend: {{ x: 0.01, y: 0.99, bgcolor: 'rgba(255,255,255,0.8)' }},
  margin: {{ l: 70, r: 30, t: 30, b: 70 }}
}};
Plotly.newPlot('plot', [diagonal, green, blue, orange, red], layout);
</script>
</body>
</html>"""

    with open(output_html, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"Scatter plot written to: {output_html}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    p = argparse.ArgumentParser(
        description="Compare two PaSTTeL CSV benchmark files and produce a scatter plot.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--csv-x",    required=True, metavar="CSV",
                   help="CSV file for the X axis (e.g. Z3 results)")
    p.add_argument("--csv-y",    required=True, metavar="CSV",
                   help="CSV file for the Y axis (e.g. CVC5 results)")
    p.add_argument("--col",      default="pasttel (ms)", metavar="COL",
                   help="Time column used for both axes (default: 'pasttel (ms)')")
    p.add_argument("--col-x",    default=None, metavar="COL",
                   help="Override time column for X axis only")
    p.add_argument("--col-y",    default=None, metavar="COL",
                   help="Override time column for Y axis only")
    p.add_argument("--label-x",  default=None, metavar="LABEL",
                   help="Display name for X axis (default: stem of --csv-x)")
    p.add_argument("--label-y",  default=None, metavar="LABEL",
                   help="Display name for Y axis (default: stem of --csv-y)")
    p.add_argument("--timeout",  type=float, default=700.0, metavar="S",
                   help="Timeout in seconds used for PAR-2 penalty (default: 700)")
    p.add_argument("--output",   default=None, metavar="HTML",
                   help="Output HTML file (default: compare_<label_x>_vs_<label_y>.html)")
    p.add_argument("--log",      action="store_true",
                   help="Use logarithmic scale on both axes")

    args = p.parse_args()

    col_x = args.col_x or args.col
    col_y = args.col_y or args.col

    label_x = args.label_x or os.path.splitext(os.path.basename(args.csv_x))[0]
    label_y = args.label_y or os.path.splitext(os.path.basename(args.csv_y))[0]

    output = args.output or f"compare_{label_x}_vs_{label_y}.html"

    compare_and_plot(
        csv_x=args.csv_x,
        csv_y=args.csv_y,
        col_x=col_x,
        col_y=col_y,
        label_x=label_x,
        label_y=label_y,
        timeout_s=args.timeout,
        output_html=output,
        log_scale=args.log,
    )


if __name__ == "__main__":
    main()
