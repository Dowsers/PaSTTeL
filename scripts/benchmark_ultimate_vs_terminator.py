#!/usr/bin/env python3
"""
Benchmark Pipeline: Ultimate LassoRanker vs Terminator

Parses Ultimate lasso trace files, converts them to JSON for the terminator tool,
runs both tools, and produces a CSV comparison of results and timing.

Usage:
    python3 scripts/benchmark_ultimate_vs_terminator.py \
        --input-dir /path/to/lasso_traces/ \
        --terminator-bin ./bin/terminator \
        --output results.csv
"""

import argparse
import csv
import glob
import json
import os
import re
import subprocess
import sys


# =============================================================================
# Algo NAME MAPPING
# =============================================================================

TERMINATOR_ALGO_MAP = {
    "RankingBased(AffineTemplate)": "Affine template",
    "RankingBased(NestedTemplate)": "Nested template",
    "RankingBased(LexicographicTemplate)": "Lexicographic Template",
    "FixpointTechnique": "Fixpoint",
    "Fixpoint": "Fixpoint",
    "GeometricTechnique": "GNTA",
    "Geometric(3)": "GNTA",
    "Geometric(1)": "GNTA",
    "Unknown": None,  # skip unknown entries
}


def normalize_european_float(s):
    """Convert European decimal separator (comma) to dot."""
    return s.replace(",", ".")



def sanitize_identifier(s):
    """Remove ~, #, | characters that cause issues in the terminator parser."""
    s = re.sub(r'old\(([^()]*)\)', r'old_\1_', s)
    return s.replace("|", "").replace("~", "").replace("#", "")


def collect_ssa_vars(in_vars, out_vars):
    """Collect all SSA variable names from in_vars and out_vars mappings."""
    ssa = set()
    for v in in_vars.values():
        ssa.add(v.strip("|"))
    for v in out_vars.values():
        ssa.add(v.strip("|"))
    return ssa


def parse_sexp_toplevel(formula):
    """Split a top-level (and ...) into its conjuncts.

    Returns a list of conjunct strings. If the formula is not a conjunction,
    returns [formula].
    """
    formula = formula.strip()
    if not formula.startswith("(and "):
        return [formula]

    # Remove outer (and ... )
    inner = formula[5:-1].strip()

    # Split into top-level S-expressions
    conjuncts = []
    depth = 0
    start = 0
    for i, ch in enumerate(inner):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == " " and depth == 0:
            token = inner[start:i].strip()
            if token:
                conjuncts.append(token)
            start = i + 1
    # Last token
    token = inner[start:].strip()
    if token:
        conjuncts.append(token)

    return conjuncts


def filter_formula_by_vars(formula, known_ssa_vars):
    """Remove conjuncts that only reference unknown SSA variables.

    Keeps a conjunct if it references at least one variable from known_ssa_vars.
    This removes irrelevant constraints on dangling variables that inflate
    polyhedra and cause std::bad_alloc during Motzkin transformations.
    """
    conjuncts = parse_sexp_toplevel(formula)
    if len(conjuncts) <= 1:
        return formula

    kept = []
    for conj in conjuncts:
        # Extract all |var_name| references from the conjunct
        refs = re.findall(r'\|([^|]+)\|', conj)
        if not refs:
            # No variable references (e.g. constant constraint) - keep it
            kept.append(conj)
            continue
        # Keep if at least one referenced var is known
        if any(r in known_ssa_vars for r in refs):
            kept.append(conj)

    if not kept:
        return "true"
    if len(kept) == 1:
        return kept[0]
    return "(and " + " ".join(kept) + ")"


# =============================================================================
# PARSE ULTIMATE LASSO TRACE
# =============================================================================

def parse_vars_mapping(text):
    """Parse 'InVars {k1=v1, k2=v2}' or 'OutVars{k1=v1, k2=v2}' into a dict.

    Variable names and SSA names can contain |...|, #, ~, . characters.
    """
    # Find the content between { and }
    m = re.search(r'\{(.*)\}', text)
    if not m:
        return {}
    content = m.group(1).strip()
    if not content:
        return {}

    result = {}
    # Split on ", " but be careful with variable names containing special chars
    # Format: varname=ssaname, varname2=ssaname2
    # SSA names are |...|  quoted
    pairs = re.findall(r'([^,=]+)=(\|[^|]*\||[^,]+)', content)
    for var_name, ssa_name in pairs:
        var_name = var_name.strip()
        ssa_name = ssa_name.strip()
        result[var_name] = ssa_name
    return result


def parse_list(text):
    """Parse 'AuxVars[v1, v2]' or 'AssignedVars[v1, v2]' into a list."""
    m = re.search(r'\[(.*)\]', text)
    if not m:
        return []
    content = m.group(1).strip()
    if not content:
        return []
    return [x.strip() for x in content.split(",") if x.strip()]


def parse_transformula_block(lines):
    """Parse a TransFormula block (Stem or Loop) from lines of text.

    Returns dict with formula, in_vars, out_vars, aux_vars, assigned_vars,
    or None if the stem is N/A (infeasible).
    """
    # Join without separator: the file wraps long lines mid-word
    # (e.g. "Out\nVars{" should become "OutVars{")
    full_text = "".join(line.strip() for line in lines)

    # Check for N/A stem
    if "N/A" in full_text or "INFEASIBLE" in full_text.upper():
        return None

    # Extract formula
    formula_match = re.search(r'Formula:\s*(.+?)\s+InVars\s', full_text)
    if not formula_match:
        return None
    formula = formula_match.group(1).strip()

    # Extract InVars
    invars_match = re.search(r'InVars\s*(\{[^}]*\})', full_text)
    in_vars = parse_vars_mapping(invars_match.group(0)) if invars_match else {}

    # Extract OutVars
    outvars_match = re.search(r'OutVars\s*(\{[^}]*\})', full_text)
    out_vars = parse_vars_mapping(outvars_match.group(0)) if outvars_match else {}

    # Extract AuxVars
    auxvars_match = re.search(r'AuxVars\s*(\[[^\]]*\])', full_text)
    aux_vars = parse_list(auxvars_match.group(0)) if auxvars_match else []

    # Extract AssignedVars
    assigned_match = re.search(r'AssignedVars\s*(\[[^\]]*\])', full_text)
    assigned_vars = parse_list(assigned_match.group(0)) if assigned_match else []

    return {
        "formula": formula,
        "in_vars": in_vars,
        "out_vars": out_vars,
        "aux_vars": aux_vars,
        "assigned_vars": assigned_vars,
    }


def parse_ultimate_trace(filepath, check_mode="lasso"):
    """Parse an Ultimate lasso trace .txt file.

    Args:
        filepath: path to the trace file
        check_mode: 'loop' to use Loop termination field only,
                     'lasso' to use Lasso termination field only (default)

    Returns a dict with:
        result: TERMINATING | NONTERMINATING | UNKNOWN
        time_ms: float (time in ms for the winning technique)
        algo: str (algorithm name)
        size: int (number of transitions)
        variables: dict {name: type}
        stem: dict or None (parsed TransFormula)
        loop: dict or None (parsed TransFormula)
    """
    with open(filepath, "r") as f:
        content = f.read()
    lines = content.split("\n")

    # --- Parse variable types ---
    variables = {}
    in_vars_section = False
    for line in lines:
        if "--- VARIABLE TYPES" in line:
            in_vars_section = True
            continue
        if in_vars_section and line.startswith("Variables:"):
            continue
        if in_vars_section and line.startswith("Function signatures:"):
            in_vars_section = False
            continue
        if in_vars_section and line.strip().startswith("---"):
            in_vars_section = False
            continue
        if in_vars_section and ":" in line and line.strip():
            # Format: "  varname                  : Type"
            parts = line.rsplit(":", 1)
            if len(parts) == 2:
                var_name = parts[0].strip()
                var_type = parts[1].strip()
                if var_name and var_type:
                    variables[var_name] = var_type

    # --- Parse TransFormula sections ---
    stem_data = None
    loop_data = None

    # Find the LINEARIZED TRACE section
    linearized_idx = None
    for i, line in enumerate(lines):
        if "LINEARIZED TRACE" in line:
            linearized_idx = i
            break

    if linearized_idx is not None:
        # Find Stem TransFormula
        stem_lines = []
        loop_lines = []
        current = None
        for i in range(linearized_idx + 1, len(lines)):
            line = lines[i]
            if line.strip().startswith("---") and "RAW TRACE" in line:
                break
            if line.strip().startswith("---"):
                break
            if "Stem TransFormula:" in line:
                current = "stem"
                # The rest of this line might contain N/A
                rest = line.split("Stem TransFormula:", 1)[1].strip()
                if rest:
                    stem_lines.append(rest)
                continue
            if "Loop TransFormula:" in line:
                current = "loop"
                continue
            if current == "stem" and line.strip():
                stem_lines.append(line)
            elif current == "loop" and line.strip():
                loop_lines.append(line)

        if stem_lines:
            stem_data = parse_transformula_block(stem_lines)
        if loop_lines:
            loop_data = parse_transformula_block(loop_lines)

    # --- Parse RAW TRACE for Stem/Loop sizes ---
    stem_size = 0
    loop_size = 0
    for line in lines:
        m = re.match(r'\s*Stem\s+\(length=(\d+)\):', line)
        if m:
            stem_size = int(m.group(1))
        m = re.match(r'\s*Loop\s+\(length=(\d+)\):', line)
        if m:
            loop_size = int(m.group(1))

    # --- Parse result ---
    result = "UNKNOWN"

    stem_feasibility = None
    loop_feasibility = None
    concat_feasibility = None
    loop_termination = None
    lasso_termination = None
    fixpoint_result = None

    for line in lines:
        m = re.match(r'\s*Stem feasibility:\s+(\w+)', line)
        if m:
            stem_feasibility = m.group(1).strip()
        m = re.match(r'\s*Loop feasibility:\s+(\w+)', line)
        if m:
            loop_feasibility = m.group(1).strip()
        m = re.match(r'\s*Concat feasibility:\s+(\w+)', line)
        if m:
            concat_feasibility = m.group(1).strip()

        m = re.match(r'\s*Loop termination result:\s+(\w+)', line)
        if m:
            loop_termination = m.group(1).strip()
        m = re.match(r'\s*Loop termination:\s+(\w+)', line)
        if m:
            lasso_termination = m.group(1).strip()
        m = re.match(r'\s*Lasso termination:\s+(\w+)', line)
        if m:
            fixpoint_result = m.group(1).strip()

    # Determine result based on check_mode:
    #   lasso_termination variable = "Loop termination:" field in trace
    #   fixpoint_result variable   = "Lasso termination:" field in trace
    if check_mode == "loop":
        # Use only the "Loop termination" field
        check_field = lasso_termination
    else:  # lasso
        # Use only the "Lasso termination" field
        check_field = fixpoint_result

    if check_field == "NONTERMINATING":
        result = "NONTERMINATING"
    elif check_field == "TERMINATING":
        result = "TERMINATING"
    elif check_field == "UNCHECKED":
        result = "UNCHECKED"

    # if infeasible anywhere, overall result is INFEASIBLE
    if loop_feasibility == "INFEASIBLE" or stem_feasibility == "INFEASIBLE" or concat_feasibility == "INFEASIBLE":
        result = "INFEASIBLE"

    print(f"****result ultimate (--check {check_mode}): ", result)

    # --- Parse nontermination argument type ---
    nonterm_argument_type = None
    for line in lines:
        m = re.match(r'\s*Type:\s+(GeometricNonTerminationArgument|InfiniteFixpointRepetitionWithExecution)', line)
        if m:
            nonterm_argument_type = m.group(1).strip()
            break

    # --- Parse fixpoint check result (YES/NO) ---
    fixpoint_check_result = None
    for line in lines:
        m = re.match(r'\s*Result:\s+(YES|NO)', line)
        if m:
            fixpoint_check_result = m.group(1).strip()
            break

    # --- Parse fixpoint check time (always, for the dedicated column) ---
    fixpoint_time_ms = 0.0
    for line in lines:
        m = re.search(r'Fixpoint check time:\s+([\d,]+)\s*ms', line)
        if m:
            fixpoint_time_ms = float(normalize_european_float(m.group(1)))
            break

    # --- Parse timing and algorithm ---
    time_ms = 0.0
    algo = "-"

    if result == "NONTERMINATING":
        # Determine which technique found nontermination
        if nonterm_argument_type == "GeometricNonTerminationArgument":
            algo = "GNTA"
            # Use total nontermination analysis time for GNTA
            for line in lines:
                m = re.search(r'Total nontermination analysis time:\s+([\d,]+)\s*ms', line)
                if m:
                    time_ms = float(normalize_european_float(m.group(1)))
                    break
            # Fallback to Total LassoRanker time if nontermination time not found
            if time_ms == 0.0:
                for line in lines:
                    m = re.search(r'Total LassoRanker time:\s+([\d,]+)\s*ms', line)
                    if m:
                        time_ms = float(normalize_european_float(m.group(1)))
                        break
        elif nonterm_argument_type == "InfiniteFixpointRepetitionWithExecution" or fixpoint_check_result == "YES":
            algo = "Fixpoint"
            time_ms = fixpoint_time_ms
        else:
            # Unknown nontermination type, use total LassoRanker time
            for line in lines:
                m = re.search(r'Total LassoRanker time:\s+([\d,]+)\s*ms', line)
                if m:
                    time_ms = float(normalize_european_float(m.group(1)))
                    break
    elif result == "TERMINATING":
        # Get total termination analysis time
        for line in lines:
            m = re.search(r'Total termination analysis time:\s+([\d,]+)\s*ms', line)
            if m:
                time_ms = float(normalize_european_float(m.group(1)))
                break
        # Find which template succeeded
        for line in lines:
            m = re.search(r'Template:\s+(\w+).*Satisfiability:\s+sat', line)
            if m:
                template_name = m.group(1).strip()
                if template_name == "affine":
                    algo = "Affine template"
                elif template_name == "nested":
                    algo = "Nested template"
                elif template_name == "lexicographic":
                    algo = "Lexicographic Template"
                else:
                    algo = template_name.capitalize() + " template"
                break

    # --- Compute size ---
    size = 0
    if stem_data is not None:
        size += 1
    if loop_data is not None:
        size += 1

    if size == 0 :
        result = "UNCHECKED"

    return {
        "result": result,
        "time_ms": time_ms,
        "algo": algo,
        "size": size,
        "stem_size": stem_size,
        "loop_size": loop_size,
        "fixpoint_time_ms": fixpoint_time_ms,
        "variables": variables,
        "stem": stem_data,
        "loop": loop_data,
    }


# =============================================================================
# CONVERT TO JSON FOR TERMINATOR
# =============================================================================

def convert_to_json(parsed):
    """Convert parsed Ultimate trace data to JSON dict for terminator."""
    # Program variables: union of ALL variables from stem and loop InVars/OutVars.
    # The C++ parser generates fresh SSA vars for any program variable missing
    # from a transition's in/out mappings, so it's safe to include everything.
    referenced_vars = set()
    if parsed["stem"] is not None:
        referenced_vars.update(parsed["stem"]["in_vars"].keys())
        referenced_vars.update(parsed["stem"]["out_vars"].keys())
    if parsed["loop"] is not None:
        referenced_vars.update(parsed["loop"]["in_vars"].keys())
        referenced_vars.update(parsed["loop"]["out_vars"].keys())

    program_vars = sorted(referenced_vars)

    # Build var_types: map each program variable to its type
    var_types = {}
    array_vars = {}
    for var_name in program_vars:
        if var_name in parsed["variables"]:
            var_type = parsed["variables"][var_name]
            var_types[var_name] = var_type
            if var_type.startswith("(Array"):
                array_vars[var_name] = var_type

    json_data = {
        "program_vars": program_vars,
    }

    if var_types:
        json_data["var_types"] = var_types

    if array_vars:
        json_data["array_vars"] = array_vars

    # Build stem transitions - keep all variables faithfully
    stem = []
    if parsed["stem"] is not None:
        s = parsed["stem"]
        stem_formula = s["formula"]

        stem.append({
            "source": "S0",
            "target": "S1",
            "formula": stem_formula,
            "in_vars": s["in_vars"],
            "out_vars": s["out_vars"],
            "aux_vars": s["aux_vars"],
            "assigned_vars": s["assigned_vars"],
        })
    json_data["stem"] = stem

    # Build loop transitions - keep all variables faithfully
    loop = []
    if parsed["loop"] is not None:
        l = parsed["loop"]
        loop_formula = l["formula"]

        loop.append({
            "source": "L0",
            "target": "L1",
            "formula": loop_formula,
            "in_vars": l["in_vars"],
            "out_vars": l["out_vars"],
            "aux_vars": l["aux_vars"],
            "assigned_vars": l["assigned_vars"],
        })
    json_data["loop"] = loop

    # Sanitize all identifiers: strip |, ~, # from variable names and formulas
    json_data["program_vars"] = [sanitize_identifier(v) for v in json_data["program_vars"]]
    if "var_types" in json_data:
        json_data["var_types"] = {
            sanitize_identifier(k): v for k, v in json_data["var_types"].items()
        }
    if "array_vars" in json_data:
        json_data["array_vars"] = {
            sanitize_identifier(k): v for k, v in json_data["array_vars"].items()
        }
    for transition_list in (json_data["stem"], json_data["loop"]):
        for t in transition_list:
            t["formula"] = sanitize_identifier(t["formula"])
            t["in_vars"] = {
                sanitize_identifier(k): sanitize_identifier(v)
                for k, v in t["in_vars"].items()
            }
            t["out_vars"] = {
                sanitize_identifier(k): sanitize_identifier(v)
                for k, v in t["out_vars"].items()
            }
            t["aux_vars"] = [sanitize_identifier(v) for v in t["aux_vars"]]
            t["assigned_vars"] = [sanitize_identifier(v) for v in t["assigned_vars"]]

    return json_data


# =============================================================================
# RUN TERMINATOR
# =============================================================================

def run_terminator(json_path, terminator_bin, cpus=2, timeout_s=60):
    """Run the terminator binary on a JSON file and parse results.

    Returns dict with:
        result: TERMINATING | NON-TERMINATING | UNKNOWN
        time_ms: float
        algo: str
    """
    cmd = [terminator_bin, "-t", "both", "-c", str(cpus), "-s" , "z3", json_path]

    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout_s,
        )
        output = proc.stdout + proc.stderr
    except subprocess.TimeoutExpired:
        return {"result": "UNKNOWN", "time_ms": -1.0, "algo": "-", "error": "TIMEOUT"}
    except Exception as e:
        return {"result": "UNKNOWN", "time_ms": -1.0, "algo": "-", "error": str(e)}

    # If output contains a Warning, treat as UNKNOWN (parsing limitation)
    if re.search(r'Warning', output):
        return {"result": "UNKNOWN", "time_ms": -1.0, "algo": "-", "error": "WARNING in output"}

    # Parse OVERALL RESULT
    result = "UNKNOWN"
    m = re.search(r'OVERALL RESULT:\s*(.+)', output)
    if m:
        raw_result = m.group(1).strip()
        if "TERMINATING" in raw_result and "NON" not in raw_result:
            result = "TERMINATING"
        elif "NON-TERMINATING" in raw_result or "NON_TERMINATING" in raw_result:
            result = "NONTERMINATING"

    # Parse TOTAL TIME
    time_ms = 0.0
    if result == "TERMINATING" :
        m = re.search(r'TERMINATING TIME:\s*([\d.]+)\s*s', output)
    elif result == "NONTERMINATING" :
        m = re.search(r'NON-TERMINATING TIME:\s*([\d.]+)\s*s', output)
    else :
        m = re.search(r'TOTAL TIME:\s*([\d.]+)\s*s', output)
    
    if m:
        time_ms = float(m.group(1)) * 1000.0  # convert s to ms

    # Parse which technique succeeded
    algo = "-"
    # Look for technique lines with TERMINATING or NON-TERM result
    # Format: "Fixpoint                      NON-TERM       0.002       "
    # The technique name is the first whitespace-delimited token on lines
    # containing TERMINATING or NON-TERM (but not header/separator lines)
    for line in output.split("\n"):
        stripped = line.strip()
        # Skip header lines, separator lines, and OVERALL RESULT line
        if not stripped or stripped.startswith("---") or stripped.startswith("="):
            continue
        if stripped.startswith("Technique") or stripped.startswith("OVERALL"):
            continue
        if "TERMINATING" in stripped or "NON-TERM" in stripped:
            parts = stripped.split()
            if len(parts) >= 2:
                raw_name = parts[0]
                mapped = TERMINATOR_ALGO_MAP.get(raw_name)
                if mapped is not None:
                    algo = mapped
                    break

    return {"result": result, "time_ms": time_ms, "algo": algo}


# =============================================================================
# DETERMINE COMBINED RESULT AND Algo
# =============================================================================

def determine_result_code(ultimate, terminator):
    """Determine the Result Code for the CSV row.

    Use Ultimate as ground truth if available, otherwise use terminator.
    """
    if ultimate["result"] != "UNKNOWN":
        return ultimate["result"]
    if terminator["result"] != "UNKNOWN":
        return terminator["result"]
    return "UNKNOWN"


def determine_algo(ultimate, terminator):
    """Determine the Algo column: both algorithms separated by /."""
    u_algo = ultimate["algo"] if ultimate["algo"] != "-" else None
    t_algo = terminator["algo"] if terminator["algo"] != "-" else None

    if u_algo and t_algo:
        return f"{u_algo} / {t_algo}"
    elif u_algo:
        return u_algo
    elif t_algo:
        return t_algo
    return "-"


# =============================================================================
# SCATTER PLOT GENERATION
# =============================================================================

def generate_scatter_plot(csv_path, output_html, timeout_s=60, log_scale=False):
    """Read the benchmark CSV and generate an interactive HTML scatter plot.

    Points are colored:
      - Green: both agree TERMINATING
      - Blue: both agree NONTERMINATING
      - Red: Ultimate has a result but Terminator disagrees or has no answer

    When one approach returns UNKNOWN while the other has a result,
    the UNKNOWN approach gets a PAR-2 penalty time (timeout * 2) in the plot.

    Rows with INFEASIBLE / UNCHECKED / missing times are skipped.
    A dashed y=x line is drawn for reference.
    """
    par2_ms = timeout_s * 2 * 1000.0  # PAR-2 penalty in ms
    rows = []
    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)

    # Classify each row into plot-worthy categories
    green_x, green_y, green_labels = [], [], []
    blue_x, blue_y, blue_labels = [], [], []
    red_x, red_y, red_labels = [], [], []

    for row in rows:
        result = row["Result Code"].strip()
        u_time_str = row["Ultimate (ms)"].strip()
        t_time_str = row["terminator (ms)"].strip()
        name = row["Trace Name"].strip()

        # Skip infeasible / unchecked / both unknown
        if result in ("INFEASIBLE", "UNCHECKED", "UNKNOWN"):
            continue

        # Handle cases where one approach has no time (UNKNOWN/timeout)
        # Use PAR-2 penalty for the UNKNOWN approach
        if u_time_str == "-" or t_time_str == "-":
            if result in ("TERMINATING", "NONTERMINATING"):
                try:
                    ux = float(u_time_str) if u_time_str != "-" else par2_ms
                    ty = float(t_time_str) if t_time_str != "-" else par2_ms
                except ValueError:
                    continue
                unknown_side = "Terminator" if t_time_str == "-" else "Ultimate"
                red_x.append(ux)
                red_y.append(ty)
                red_labels.append(f"{name}<br>Ultimate: {result}<br>{unknown_side}: UNKNOWN (PAR-2={par2_ms:.0f}ms)")
            continue

        try:
            ux = float(u_time_str)
            ty = float(t_time_str)
        except ValueError:
            continue

        # Determine the Ultimate and Terminator individual results
        # The CSV "Result Code" is the combined result. We need to figure out
        # if they agree or disagree.  Heuristic: if result is TERMINATING or
        # NONTERMINATING and both have valid times, they likely agree unless
        # algo column hints otherwise.  A more robust approach: re-derive from
        # the result code + the fact that both returned a time.
        # Since determine_result_code picks Ultimate first, we check if
        # terminator could contradict.  The simplest reliable signal: if
        # result is TERMINATING, Ultimate said TERMINATING.  If terminator
        # also returned a time, it found *something* — but it might have found
        # NONTERMINATING.  We can detect contradiction from the Algo field:
        # if Algo contains "Fixpoint" or "GNTA" it means nontermination was found
        # by that tool.

        algo = row.get("Algo", "").strip()
        # Determine per-tool verdicts from algo string
        algo_parts = [p.strip() for p in algo.split("/")] if "/" in algo else [algo]

        t_algo = algo_parts[-1] if len(algo_parts) >= 2 else ""

        def verdict_from_algo(a):
            a = a.strip().lower()
            if a in ("fixpoint", "gnta"):
                return "NONTERMINATING"
            if "template" in a:
                return "TERMINATING"
            return "UNKNOWN"

        u_verdict = result  # Ultimate is ground truth for Result Code
        t_verdict = verdict_from_algo(t_algo) if t_algo else "UNKNOWN"

        # If terminator has no algo info, it returned UNKNOWN (no answer/timeout)
        # Do NOT assume agreement — leave t_verdict as UNKNOWN so it goes to red

        # Classify
        if u_verdict == "TERMINATING" and t_verdict == "TERMINATING":
            green_x.append(ux)
            green_y.append(ty)
            green_labels.append(f"{name}<br>Algo: {algo}<br>U={ux:.1f}ms T={ty:.1f}ms")
        elif u_verdict == "NONTERMINATING" and t_verdict == "NONTERMINATING":
            blue_x.append(ux)
            blue_y.append(ty)
            blue_labels.append(f"{name}<br>Algo: {algo}<br>U={ux:.1f}ms T={ty:.1f}ms")
        else:
            # Disagreement or unexpected combo
            red_x.append(ux)
            red_y.append(ty)
            red_labels.append(
                f"{name}<br>Algo: {algo}<br>U={ux:.1f}ms T={ty:.1f}ms"
                f"<br>Ultimate: {u_verdict}, Terminator: {t_verdict}"
            )

    all_y = green_y + blue_y + red_y
    all_x = green_x + blue_x + red_x
    if not all_x:
        print("No plottable data points found (all INFEASIBLE/UNCHECKED or missing times).")
        return

    max_val = max(max(all_x), max(all_y)) if all_y else max(all_x)

    # Build the HTML with Plotly
    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Ultimate vs Terminator - Scatter Plot</title>
<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>
<style>
  body {{ font-family: Arial, sans-serif; margin: 20px; }}
  #plot {{ width: 100%; height: 85vh; }}
</style>
</head>
<body>
<h2>Ultimate vs Terminator &mdash; Computation Time Comparison</h2>
<p>
  <span style="color:green;">&#9679;</span> Terminating &nbsp;
  <span style="color:blue;">&#9679;</span> Non-terminating &nbsp;
  <span style="color:red;">&#9679;</span> Unknown
</p>
<div id="plot"></div>
<script>
var green = {{
  x: {json.dumps(green_x)},
  y: {json.dumps(green_y)},
  text: {json.dumps(green_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Terminating ({len(green_x)})',
  marker: {{ color: 'green', size: 8, opacity: 0.7 }},
  hoverinfo: 'text'
}};
var blue = {{
  x: {json.dumps(blue_x)},
  y: {json.dumps(blue_y)},
  text: {json.dumps(blue_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Non-terminating ({len(blue_x)})',
  marker: {{ color: 'blue', size: 8, opacity: 0.7 }},
  hoverinfo: 'text'
}};
var red = {{
  x: {json.dumps(red_x)},
  y: {json.dumps(red_y)},
  text: {json.dumps(red_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Unknown ({len(red_x)})',
  marker: {{ color: 'red', size: 10, opacity: 0.85, symbol: 'x' }},
  hoverinfo: 'text'
}};
var diag_max = {max_val * 1.05};
var diagonal = {{
  x: [0, diag_max],
  y: [0, diag_max],
  mode: 'lines',
  type: 'scatter',
  name: 'y = x',
  line: {{ color: 'gray', width: 1.5, dash: 'dash' }},
  hoverinfo: 'skip',
  showlegend: true
}};
var layout = {{
  xaxis: {{
    title: 'Ultimate LassoRanker (ms)',
    {"type: 'log'," if log_scale else "rangemode: 'tozero',"}
  }},
  yaxis: {{
    title: 'Terminator (ms)',
    {"type: 'log'," if log_scale else "rangemode: 'tozero',"}
  }},
  hovermode: 'closest',
  legend: {{ x: 0.01, y: 0.99, bgcolor: 'rgba(255,255,255,0.8)' }},
  margin: {{ l: 70, r: 30, t: 30, b: 70 }}
}};
Plotly.newPlot('plot', [diagonal, green, blue, red], layout);
</script>
</body>
</html>"""

    with open(output_html, "w") as f:
        f.write(html)
    print(f"Scatter plot written to: {output_html}")


# =============================================================================
# MAIN PIPELINE
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark Ultimate LassoRanker vs Terminator"
    )
    parser.add_argument(
        "--input-dir", default=None,
        help="Directory starting with lass*.txt files"
    )
    parser.add_argument(
        "--terminator-bin", default=None,
        help="Path to the terminator binary"
    )
    parser.add_argument(
        "--output", default="benchmark_results.csv",
        help="Output CSV file (default: benchmark_results.csv)"
    )
    parser.add_argument(
        "--cpus", type=int, default=2,
        help="Number of CPUs for terminator (default: 2)"
    )
    parser.add_argument(
        "--timeout", type=int, default=60,
        help="Timeout in seconds for each terminator run (default: 60)"
    )
    parser.add_argument(
        "--plot", nargs="?", const=True, default=False,
        metavar="CSV_FILE",
        help="Generate scatter plot. Use alone with a CSV file to only plot "
             "(skip benchmarking), or add to a benchmark run to plot after."
    )
    parser.add_argument(
        "--log", action="store_true", default=False,
        help="Use logarithmic scale for the scatter plot axes"
    )
    parser.add_argument(
        "--check", choices=["loop", "lasso"], default="lasso",
        help="Which Ultimate result field to use: 'loop' uses Loop termination, "
             "'lasso' uses Lasso termination (default: lasso)"
    )
    args = parser.parse_args()

    # Plot-only mode: --plot CSV_FILE (no benchmarking)
    if isinstance(args.plot, str):
        csv_file = args.plot
        if not os.path.isfile(csv_file):
            print(f"Error: CSV file not found: {csv_file}")
            sys.exit(1)
        html_out = os.path.splitext(csv_file)[0] + "_scatter.html"
        generate_scatter_plot(csv_file, html_out, timeout_s=args.timeout, log_scale=args.log)
        return

    # Benchmark mode requires --input-dir and --terminator-bin
    if not args.input_dir or not args.terminator_bin:
        parser.error("--input-dir and --terminator-bin are required for benchmarking")

    # Find all trace files
    pattern = os.path.join(args.input_dir, "lass*.txt")
    trace_files = sorted(glob.glob(pattern))

    if not trace_files:
        print(f"No lass*.txt files found in {args.input_dir}")
        sys.exit(1)

    print(f"Found {len(trace_files)} trace file(s) in {args.input_dir}")

    # Check terminator binary exists
    if not os.path.isfile(args.terminator_bin):
        print(f"Error: terminator binary not found at {args.terminator_bin}")
        sys.exit(1)

    # Create a directory next to the output CSV to store converted JSON files
    output_dir = os.path.dirname(os.path.abspath(args.output))
    #json_dir = os.path.join(output_dir, "json_traces")
    #os.makedirs(json_dir, exist_ok=True)
    #print(f"JSON files will be saved in: {json_dir}")

    results = []

    for trace_file in trace_files:
        basename = os.path.basename(trace_file)
        dirname=os.path.dirname(trace_file)
        
        print("** OUTPUT name ",dirname)
        print(f"\n{'='*60}")
        print(f"Processing: {basename}")
        print(f"{'='*60}")

        # 1. Parse Ultimate trace
        try:
            ultimate = parse_ultimate_trace(trace_file, check_mode=args.check)
        except Exception as e:
            print(f"  ERROR parsing Ultimate trace: {e}")
            results.append({
                "Trace Name": trace_file,
                "Result Code": "UNKNOWN",
                "Ultimate-Fixpoint (ms)": "-",
                "Ultimate (ms)": "-",
                "terminator (ms)": "-",
                "Stem Size": 0,
                "Loop Size": 0,
                "Total Size Trace": 0,
                "Algo": "-",
            })
            continue

        print(f"  Ultimate result: {ultimate['result']}")
        print(f"  Ultimate time:   {ultimate['time_ms']} ms")
        print(f"  Ultimate algo:   {ultimate['algo']}")
        print(f"  Trace size:      {ultimate['size']} transition(s)")
        
        if ultimate['size'] == 0:
            print("  Skipping terminator run due to zero-size trace.")
            results.append({
                "Trace Name": trace_file,
                "Result Code": ultimate['result'],
                "Ultimate-Fixpoint (ms)": f"{ultimate['fixpoint_time_ms']:.2f}" if ultimate['fixpoint_time_ms'] > 0 else "-",
                "Ultimate (ms)": f"{ultimate['time_ms']:.2f}" if ultimate['time_ms'] >= 0 else "-",
                "terminator (ms)": "-",
                "Stem Size": ultimate['stem_size'],
                "Loop Size": ultimate['loop_size'],
                "Total Size Trace": ultimate['stem_size'] + ultimate['loop_size'],
                "Algo": ultimate['algo'],
            })
            continue
        if ultimate['result'] == "UNCHECKED" or ultimate['result'] == "UNKNOWN":
            print("  Skipping terminator run due to unchecked or unknown trace.")
            results.append({
                "Trace Name": trace_file,
                "Result Code": ultimate['result'],
                "Ultimate-Fixpoint (ms)": f"{ultimate['fixpoint_time_ms']:.2f}" if ultimate['fixpoint_time_ms'] > 0 else "-",
                "Ultimate (ms)": f"{ultimate['time_ms']:.2f}" if ultimate['time_ms'] >= 0 else "-",
                "terminator (ms)": "-",
                "Stem Size": ultimate['stem_size'],
                "Loop Size": ultimate['loop_size'],
                "Total Size Trace": ultimate['stem_size'] + ultimate['loop_size'],
                "Algo": ultimate['algo'],
            })
            continue
        if ultimate['result'] == "INFEASIBLE":
            print("  Skipping terminator run due to infeasible trace.")
            results.append({
                "Trace Name": trace_file,
                "Result Code": ultimate['result'],
                "Ultimate-Fixpoint (ms)": f"{ultimate['fixpoint_time_ms']:.2f}" if ultimate['fixpoint_time_ms'] > 0 else "-",
                "Ultimate (ms)": f"{ultimate['time_ms']:.2f}" if ultimate['time_ms'] >= 0 else "-",
                "terminator (ms)": "-",
                "Stem Size": ultimate['stem_size'],
                "Loop Size": ultimate['loop_size'],
                "Total Size Trace": ultimate['stem_size'] + ultimate['loop_size'],
                "Algo": ultimate['algo'],
            })
            continue

        # 2. Convert to JSON and save permanently
        json_data = convert_to_json(ultimate)

        json_name = os.path.splitext(basename)[0] + ".json"
        print("*** JSON name ",json_name)
        json_path = os.path.join(dirname, json_name)
        with open(json_path, "w") as jf:
            json.dump(json_data, jf, indent=2)

        print(f"  JSON written to: {json_path}")

        # 3. Run terminator
        print(f"  Running terminator (-t both -c {args.cpus})...")
        terminator = run_terminator(
            json_path, args.terminator_bin, cpus=args.cpus, timeout_s=args.timeout
        )

        print(f"  Terminator result: {terminator['result']}")
        print(f"  Terminator time:   {terminator['time_ms']:.2f} ms")
        print(f"  Terminator algo:   {terminator['algo']}")
        if "error" in terminator:
            print(f"  Terminator error:  {terminator['error']}")

        # 4. Build CSV row
        result_code = determine_result_code(ultimate, terminator)
        algo = determine_algo(ultimate, terminator)

        u_time = f"{ultimate['time_ms']:.2f}" if ultimate["time_ms"] >= 0 else "-"
        u_fixpoint_time = f"{ultimate['fixpoint_time_ms']:.2f}" if ultimate['fixpoint_time_ms'] > 0 else "-"
        t_time = f"{terminator['time_ms']:.2f}" if terminator["time_ms"] >= 0 else "-"

        row = {
            "Trace Name": trace_file,
            "Result Code": result_code,
            "Ultimate-Fixpoint (ms)": u_fixpoint_time,
            "Ultimate (ms)": u_time,
            "terminator (ms)": t_time,
            "Stem Size": ultimate["stem_size"],
            "Loop Size": ultimate["loop_size"],
            "Total Size Trace": ultimate["stem_size"] + ultimate["loop_size"],
            "Algo": algo,
        }
        results.append(row)

    # 5. Write CSV
    if results:
        fieldnames = [
            "Trace Name",
            "Result Code",
            "Ultimate-Fixpoint (ms)",
            "Ultimate (ms)",
            "terminator (ms)",
            "Stem Size",
            "Loop Size",
            "Total Size Trace",
            "Algo",
        ]
        with open(args.output, "a", newline="") as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            if os.path.getsize(args.output) == 0:
                writer.writeheader()
            writer.writerows(results)

        print(f"\n{'='*60}")
        print(f"Results written to: {args.output}")
        print(f"{'='*60}")

        # Print summary table
        print(f"\n{'Trace Name':<70} {'Result Code':<15} {'U-Fixpoint (ms)':<17} {'Ultimate (ms)':<15} {'terminator (ms)':<17} {'Stem':<6} {'Loop':<6} {'Total':<7} {'Algo'}")
        print("-" * 160)
        for row in results:
            print(
                f"{row['Trace Name']:<70} "
                f"{row['Result Code']:<15} "
                f"{str(row['Ultimate-Fixpoint (ms)']):<17} "
                f"{str(row['Ultimate (ms)']):<15} "
                f"{str(row['terminator (ms)']):<17} "
                f"{str(row['Stem Size']):<6} "
                f"{str(row['Loop Size']):<6} "
                f"{str(row['Total Size Trace']):<7} "
                f"{row['Algo']}"
            )

        # Generate scatter plot if requested
        if args.plot:
            html_out = os.path.splitext(args.output)[0] + "_scatter.html"
            generate_scatter_plot(args.output, html_out, timeout_s=args.timeout, log_scale=args.log)


if __name__ == "__main__":
    main()
