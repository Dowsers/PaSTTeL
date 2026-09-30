#!/usr/bin/env python3
"""
Benchmark Pipeline: Ultimate LassoRanker vs PaSTTeL

Parses Ultimate lasso trace files, converts them to JSON for the pasttel tool,
runs both tools, and produces a CSV comparison of results and timing.

Usage:
    python3 scripts/benchmark_ultimate_vs_pasttel.py \
        --input-dir /path/to/lasso_traces/ \
        --pasttel-bin ./bin/pasttel \
        --output results.csv \
        --solver z3
        --strat both
        --cpus 2
"""

import argparse
import collections
import csv
import glob
import json
import math
import os
import re
import subprocess
import sys


# =============================================================================
# Algo NAME MAPPING
# =============================================================================


def normalize_european_float(s):
    """Convert European decimal separator (comma) to dot."""
    return s.replace(",", ".")
    
def _ranking_type_ultimate_to_algo(template_name):
    """Convert a 'Ranking function type' raw string to a display name.

    Handles both simple types (affine, nested) and prefixed ones
    (2-phase, 4-nested, 2-lex, …).
    """
    name = template_name.strip().lower()

    # Strip optional numeric prefix to find the base type
    # e.g. "4-nested" → prefix="4", base="nested"
    #      "2-phase"  → prefix="2", base="phase"
    #      "2-lex"    → prefix="2", base="lex"
    #      "affine"   → prefix=None, base="affine"
    m = re.match(r'^(\d+)-(.+)$', name)
    if m:
        prefix = m.group(1)
        base   = m.group(2)
    else:
        prefix = None
        base   = name

    BASE_MAP = {
        "affine":        "Affine Template",
        "nested":        "Nested Template",
        "lex":           "Lexicographic Template",
        "lexicographic": "Lexicographic Template",
        "phase":         "Phase Template",
    }

    base_label = BASE_MAP.get(base, base.capitalize() + " Template")

    if prefix:
        return f"{prefix}-{base_label}"   # e.g. "4-Nested Template"
    return base_label                     # e.g. "Affine Template"


def _pasttel_algo_name(raw_name):
    """Normalize a PaSTTeL technique name into a canonical display name.

    Examples:
        RankingBased(AffineTemplate)        -> "Affine Template"
        RankingBased(4-NestedTemplate)      -> "4-Nested Template"
        RankingBased(LexicographicTemplate) -> "Lexicographic Template"
        FixpointTechnique / Fixpoint        -> "Fixpoint"
        GeometricTechnique / GeometricL(N)  -> "GNTA"
    """
    # RankingBased(...): extract inner name and insert space before "Template"
    m = re.match(r'RankingBased\((.+)\)', raw_name)
    if m:
        inner = m.group(1)  # e.g. "AffineTemplate" or "4-NestedTemplate"
        # Insert a space before "Template" suffix
        return re.sub(r'([A-Za-z\d])Template$', r'\1 Template', inner)

    if raw_name in ("Fixpoint", "FixpointTechnique"):
        return "Fixpoint"

    if raw_name.startswith("Geometric"):
        return "GNTA"

    return raw_name


def _normalize_strat_label(label):
    """Canonical display name for a strategy label in the TESTED STRATEGIES block.

    The block mixes short labels and full technique names:
        FIXPOINT / GNTA / AFFINE / NESTED
        RankingBased(2-5-LexicographicTemplate) / ...(2-5-MultiphaseTemplate) / ...(2-5-PiecewiseTemplate)
    Normalising both to the same canonical name used by _pasttel_algo_name lets
    us match a block entry against the winning technique's name.
    """
    l = label.strip()
    u = l.upper()
    if u == "FIXPOINT":
        return "Fixpoint"
    if u == "GNTA":
        return "GNTA"
    if u == "AFFINE":
        return "Affine Template"
    if u == "NESTED":
        return "Nested Template"
    return _pasttel_algo_name(l)


def sanitize_identifier(s):
    """Remove ~, #, | characters that cause issues in the pasttel parser."""
    s = re.sub(r'old\(([^()]*)\)', r'old_\1_', s)
    return s #s.replace("|", "").replace("~", "").replace("#", "").replace(", ",",")


def smt_quote(name):
    """Wrap name in |...| if it contains SMT-special characters and isn't already quoted.

    Identifiers like 'v_rep(select #valid 0)_2' contain parentheses and spaces
    which are invalid in unquoted SMT-LIB2 identifiers. Wrapping them in |...|
    makes them valid atomic tokens throughout the solver and parser.
    """
    if name.startswith('|') and name.endswith('|'):
        return name  # already quoted
    if any(c in name for c in ('(', ')', ' ')):
        return '|' + name + '|'
    return name


def extract_formula_ssa_vars(formula):
    """Extract all SSA variable names from a linearized formula.

    Handles both plain names (v_foo_42) and SMT-LIB2 quoted identifiers (|v_...|).
    Returns a set of raw SSA names (with pipes if quoted).
    """
    vars_found = set()
    i = 0
    n = len(formula)
    while i < n:
        if formula[i] == '|':
            # Quoted identifier: collect until closing pipe
            j = i + 1
            while j < n and formula[j] != '|':
                j += 1
            if j < n:
                vars_found.add(formula[i:j+1])  # include both pipes
                i = j + 1
            else:
                i = j
        elif formula[i] == 'v' and i + 1 < n and formula[i+1] == '_':
            # Unquoted SSA name starting with v_
            j = i
            while j < n and (formula[j].isalnum() or formula[j] in ('_', '~', '$', '.')):
                j += 1
            vars_found.add(formula[i:j])
            i = j
        else:
            i += 1
    return vars_found


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
    # Find the content between the first { and its matching }
    m = re.search(r'\{([^}]*)\}', text)
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
    """Parse 'AuxVars[v1, v2]' or 'AssignedVars[v1, v2]' into a list.

    Handles variable names containing commas inside |...| quoted identifiers,
    e.g. |v_arrayCell[base_2, (+ offset_2 loopctr_10)]_1|.
    """
    m = re.search(r'\[(.*)\]', text, re.DOTALL)
    if not m:
        return []
    content = m.group(1).strip()
    if not content:
        return []
    # Split on commas that are outside |...| pipe-quoted tokens
    items = []
    current = []
    in_pipes = False
    for ch in content:
        if ch == '|':
            in_pipes = not in_pipes
            current.append(ch)
        elif ch == ',' and not in_pipes:
            token = ''.join(current).strip()
            if token:
                items.append(token)
            current = []
        else:
            current.append(ch)
    token = ''.join(current).strip()
    if token:
        items.append(token)
    return items


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


def parse_preprocessed_linear_trace_section(lines, start_idx):
    """Parse a PREPROCESSED LINEAR TRACE section from lines starting at start_idx.

    Returns (stem_data, loop_data) where each is a dict with formula/in_vars/out_vars
    or None if not found.

    Format:
        Stem (linearized):
          Formula:  (or (and ...) ...)
          InVars:   {var=ssa, ...}
          OutVars:  {var=ssa, ...}

        Loop (linearized):
          Formula:  ...
          InVars:   {...}
          OutVars:  {...}
    """
    stem_data = None
    loop_data = None

    current = None  # 'stem' or 'loop'
    current_fields = {}  # accumulated field lines per field name

    def finalize_block(fields):
        """Build a stem/loop data dict from accumulated field lines."""
        formula = fields.get("Formula", "").strip()
        invars_text = fields.get("InVars", "").strip()
        outvars_text = fields.get("OutVars", "").strip()
        auxvars_text = fields.get("AuxVars", "").strip()
        assigned_text = fields.get("AssignedVars", "").strip()
        if not formula:
            return None
        in_vars = parse_vars_mapping("{" + invars_text.strip("{}") + "}") if invars_text else {}
        out_vars = parse_vars_mapping("{" + outvars_text.strip("{}") + "}") if outvars_text else {}
        aux_vars = parse_list("[" + auxvars_text.strip("[]") + "]") if auxvars_text else []
        assigned_vars = parse_list("[" + assigned_text.strip("[]") + "]") if assigned_text else []
        return {
            "formula": formula,
            "in_vars": in_vars,
            "out_vars": out_vars,
            "aux_vars": aux_vars,
            "assigned_vars": assigned_vars,
        }

    current_field = None  # which field we are accumulating

    for i in range(start_idx + 1, len(lines)):
        line = lines[i]
        stripped = line.strip()

        # Section end: another "---" header or empty sentinel
        if stripped.startswith("---"):
            break

        # Start of Stem block
        if re.match(r'Stem\s*\(linearized\)\s*:', stripped):
            if current is not None and current_fields:
                block = finalize_block(current_fields)
                if current == "stem":
                    stem_data = block
                else:
                    loop_data = block
            current = "stem"
            current_fields = {}
            current_field = None
            continue

        # Start of Loop block
        if re.match(r'Loop\s*\(linearized\)\s*:', stripped):
            if current is not None and current_fields:
                block = finalize_block(current_fields)
                if current == "stem":
                    stem_data = block
                else:
                    loop_data = block
            current = "loop"
            current_fields = {}
            current_field = None
            continue

        if current is None:
            continue

        # Named field line: "  Formula:  ..." / "  InVars:   ..." / "  OutVars:  ..." / "  AuxVars:  ..." / "  AssignedVars:  ..."
        m = re.match(r'\s+(Formula|InVars|OutVars|AuxVars|AssignedVars)\s*:\s*(.*)', line)
        if m:
            current_field = m.group(1)
            current_fields[current_field] = m.group(2)
            continue

        # Continuation line for current field (long formula wrapped)
        if current_field is not None and stripped:
            current_fields[current_field] += stripped

    # Finalize last block
    if current is not None and current_fields:
        block = finalize_block(current_fields)
        if current == "stem":
            stem_data = block
        else:
            loop_data = block

    return stem_data, loop_data


def distinct_template_runs_ms(content):
    """Termination-analysis time of the distinct template runs (ms), or None.

    LassoCheck re-appends its cumulative benchmark list after each template, so
    the dump repeats earlier runs (same template, same ns) and over-counts them
    in 'Termination analysis'. A (template, ns) pair is one real run; per-component
    runs (partitioner=ON) differ in ns and are kept.
    """
    sections = content.split("--- TERMINATION ANALYSIS BENCHMARKS")
    if len(sections) < 2:
        return None
    section = sections[-1].split("\n---", 1)[0]
    runs = re.findall(r'\[\d+\] Template:\s+(\S+).*?Time:\s+(\d+) ns', section)
    if not runs:
        return None
    return sum(int(ns) for _, ns in dict.fromkeys(runs)) / 1e6


def parse_ultimate_trace(filepath, check_mode="lasso", parse_mode="normal"):
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

    if parse_mode == "preprocess":
        # Find the PREPROCESSED LINEAR TRACE section
        preproc_idx = None
        for i, line in enumerate(lines):
            if "PREPROCESSED LINEAR TRACE" in line:
                preproc_idx = i
                break
        if preproc_idx is not None:
            stem_data, loop_data = parse_preprocessed_linear_trace_section(lines, preproc_idx)
    else:
        # Find the LINEARIZED TRACE section (normal mode)
        # Skip "PREPROCESSED LINEAR TRACE" lines to get the first plain "LINEARIZED TRACE"
        linearized_idx = None
        for i, line in enumerate(lines):
            if "LINEARIZED TRACE" in line and "PREPROCESSED" not in line:
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

    # --- Parse timing breakdown (always, for dedicated columns) ---
    fixpoint_time_ms = 0.0
    termination_time_ms = 0.0
    nontermination_time_ms = 0.0
    for line in lines:
        m = re.search(r'Fixpoint check time:\s+([\d,]+)\s*ms', line)
        if m:
            fixpoint_time_ms = float(normalize_european_float(m.group(1)))
        m = re.search(r'Termination analysis:\s+([\d,]+)\s*ms', line)
        if m:
            termination_time_ms = float(normalize_european_float(m.group(1)))
        m = re.search(r'Nontermination analysis:\s+([\d,]+)\s*ms', line)
        if m:
            nontermination_time_ms = float(normalize_european_float(m.group(1)))

    real_term_ms = distinct_template_runs_ms(content)
    if real_term_ms is not None:
        if abs(real_term_ms - termination_time_ms) > 0.01:
            print(f"  Termination time: {real_term_ms:.2f} ms "
                  f"(dump total {termination_time_ms:.2f} ms counted repeated runs)")
        termination_time_ms = real_term_ms

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
            # Fallback to the LassoRanker time if nontermination time not found
            if time_ms == 0.0:
                time_ms = termination_time_ms + nontermination_time_ms
        elif nonterm_argument_type == "InfiniteFixpointRepetitionWithExecution" or fixpoint_check_result == "YES":
            algo = "Fixpoint"
            time_ms = fixpoint_time_ms
        else:
            # Unknown nontermination type, use the LassoRanker time
            time_ms = termination_time_ms + nontermination_time_ms
    elif result == "TERMINATING":
        time_ms = termination_time_ms
	# Find which template succeeded
        for line in lines:
            # [\w\-]+ captures both simple names ("affine") and
            # dash-prefixed ones ("4-nested", "2-phase", "2-lex")
            m = re.search(r'Ranking function type:\s+([\w\-]+)', line)
            if m:
                algo = _ranking_type_ultimate_to_algo(m.group(1))
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
        "termination_time_ms": termination_time_ms,
        "nontermination_time_ms": nontermination_time_ms,
        "variables": variables,
        "stem": stem_data,
        "loop": loop_data,
    }


# =============================================================================
# CONVERT TO JSON FOR PASTTEL
# =============================================================================

def convert_to_json(parsed):
    """Convert parsed Ultimate trace data to JSON dict for pasttel."""
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

    def build_transition_aux_vars(trans_data):
        """Return the aux_vars list for a transition.

        If aux_vars is already populated (normal mode), use it.
        Otherwise (preprocess mode), derive free SSA vars from the formula:
        variables present in the formula but absent from in_vars and out_vars.
        """
        if trans_data["aux_vars"]:
            return trans_data["aux_vars"]
        mapped_ssa = set(trans_data["in_vars"].values()) | set(trans_data["out_vars"].values())
        # Normalise: strip surrounding pipes for comparison (in_vars/out_vars values may have pipes)
        mapped_ssa_norm = {v.strip("|") for v in mapped_ssa}
        formula_vars = extract_formula_ssa_vars(trans_data["formula"])
        free = []
        for v in sorted(formula_vars):
            norm = v.strip("|")
            if norm not in mapped_ssa_norm:
                free.append(v)
        return free

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
            "aux_vars": build_transition_aux_vars(s),
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
            "aux_vars": build_transition_aux_vars(l),
            "assigned_vars": l["assigned_vars"],
        })
    json_data["loop"] = loop

    # Sanitize all identifiers: strip |, ~, # from variable names and formulas
    # smt_quote wraps keys containing SMT-special chars (parens, spaces) in |...|
    # so that identifiers like 'v_rep(select #valid 0)_2' become valid SMT tokens.
    json_data["program_vars"] = [smt_quote(sanitize_identifier(v)) for v in json_data["program_vars"]]
    if "var_types" in json_data:
        json_data["var_types"] = {
            smt_quote(sanitize_identifier(k)): v for k, v in json_data["var_types"].items()
        }
    if "array_vars" in json_data:
        json_data["array_vars"] = {
            smt_quote(sanitize_identifier(k)): v for k, v in json_data["array_vars"].items()
        }
    for transition_list in (json_data["stem"], json_data["loop"]):
        for t in transition_list:
            t["formula"] = sanitize_identifier(t["formula"])
            t["in_vars"] = {
                smt_quote(sanitize_identifier(k)): sanitize_identifier(v)
                for k, v in t["in_vars"].items()
            }
            t["out_vars"] = {
                smt_quote(sanitize_identifier(k)): sanitize_identifier(v)
                for k, v in t["out_vars"].items()
            }
            t["aux_vars"] = [sanitize_identifier(v) for v in t["aux_vars"]]
            t["assigned_vars"] = [smt_quote(sanitize_identifier(v)) for v in t["assigned_vars"]]

    return json_data


# =============================================================================
# RUN PASTTEL
# =============================================================================

def run_pasttel(json_path, pasttel_bin, cpus=2, timeout_s=600, strat="terminate", solver="z3"):
    """Run the pasttel binary on a JSON file and parse results.

    Returns dict with:
        result:        TERMINATING | NONTERMINATING | UNKNOWN
        ulr_time_ms:   sequential cumulative time (cpus=1) or winning technique time (cpus>1)
        total_time_ms: wall-clock TOTAL TIME from the report
        algo:          winning technique name
        fixpoint_ms / gnta_ms / affine_ms / nested_ms: individual strategy times (-1 = not run)
    """
    cmd = [pasttel_bin, "-a", strat, "-c", str(cpus), "-s", solver, json_path, "-val"]

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
        output = proc.stdout + proc.stderr
    except subprocess.TimeoutExpired:
        return {"result": "UNKNOWN", "ulr_time_ms": -1.0, "total_time_ms": -1.0, "algo": "-", "error": "TIMEOUT"}
    except Exception as e:
        return {"result": "UNKNOWN", "ulr_time_ms": -1.0, "total_time_ms": -1.0, "algo": "-", "error": str(e)}

    # Parse OVERALL RESULT
    result = "UNKNOWN"
    m = re.search(r'OVERALL RESULT:\s*(.+)', output)
    if m:
        raw = m.group(1).strip()
        if "TERMINATING" in raw and "NON" not in raw:
            result = "TERMINATING"
        elif "NON-TERMINATING" in raw or "NON_TERMINATING" in raw:
            result = "NONTERMINATING"

    # A "Warning: Failed to parse formula" is only informative when nothing
    # else resolved the lasso. Under -c>1, several techniques run their own
    # independent linearization concurrently; a LOSING technique's own parse
    # failure must not override a WINNING technique's conclusive verdict that
    # shows up elsewhere in the very same output (see OVERALL RESULT above).
    if result == "UNKNOWN" and re.search(r'Warning', output):
        return {"result": "NOT SUPPORTED", "ulr_time_ms": -1.0, "total_time_ms": -1.0, "algo": "-", "error": "WARNING in output"}

    # Parse TOTAL TIME
    total_time_ms = -1.0
    mt = re.search(r'TOTAL TIME:\s*([\d.]+)\s*s', output)
    if mt:
        total_time_ms = float(mt.group(1)) * 1000.0

    # Parse per-strategy times from the TESTED STRATEGIES block, generically.
    # PaSTTeL runs up to 7 strategies, and the block mixes short labels with
    # full technique names:
    #   "  - FIXPOINT TIME: 0.003 s"                              (short label)
    #   "  - RankingBased(2-5-MultiphaseTemplate) TIME: 0.686 s"  (template label)
    #   "  - NESTED TIME: -"                                      (not run)
    # Keyed by the canonical display name so it matches the winner's algo name.
    strat_order = []          # [(canonical_name, ms)] in report order (ms=-1 if not run)
    strat_times = {}          # canonical_name -> ms
    for line in output.split("\n"):
        m = re.search(r'-\s+(.+?)\s+TIME:\s*([\d.]+)\s*s', line)
        if m:
            key = _normalize_strat_label(m.group(1))
            ms = float(m.group(2)) * 1000.0
            strat_order.append((key, ms))
            strat_times[key] = ms
            continue
        m = re.search(r'-\s+(.+?)\s+TIME:\s*-\s*$', line)
        if m:
            strat_order.append((_normalize_strat_label(m.group(1)), -1.0))

    # Parse the winning technique: its name AND its own reported time. The result
    # table row is "Technique  Result  Time(s)  Proof", so the time is the first
    # float-parsable token after the name. This is robust for ALL 7 strategies
    # (affine/nested/lexicographic/multiphase/piecewise/gnta/fixpoint).
    algo = "-"
    winner_ms = -1.0
    for line in output.split("\n"):
        stripped = line.strip()
        if not stripped or stripped.startswith("---") or stripped.startswith("="):
            continue
        if stripped.startswith("Technique") or stripped.startswith("OVERALL"):
            continue
        if "TERMINATING" in stripped or "NON-TERM" in stripped:
            parts = stripped.split()
            algo = _pasttel_algo_name(parts[0])
            for tok in parts[1:]:
                try:
                    winner_ms = float(tok) * 1000.0
                    break
                except ValueError:
                    continue
            break

    # Individual times kept in the return payload (backward compat).
    fixpoint_ms = strat_times.get("Fixpoint", -1.0)
    gnta_ms     = strat_times.get("GNTA", -1.0)
    affine_ms   = strat_times.get("Affine Template", -1.0)
    nested_ms   = strat_times.get("Nested Template", -1.0)

    # Compute P-ULR time:
    #   parallel (cpus>1):   the winning technique's own time.
    #   sequential (cpus=1): cumulative time of every strategy run up to and
    #                        including the winner (report order).
    if cpus == 1:
        ulr_time_ms = -1.0
        cumul = 0.0
        matched = False
        for key, ms in strat_order:
            if ms >= 0:
                cumul += ms
            if key == algo:
                ulr_time_ms = cumul
                matched = True
                break
        if not matched:
            ulr_time_ms = winner_ms
    else:
        ulr_time_ms = winner_ms if winner_ms >= 0 else strat_times.get(algo, -1.0)

    return {
        "result":       result,
        "ulr_time_ms":  ulr_time_ms,
        "total_time_ms": total_time_ms,
        "algo":         algo,
        "fixpoint_ms":  fixpoint_ms,
        "gnta_ms":      gnta_ms,
        "affine_ms":    affine_ms,
        "nested_ms":    nested_ms,
    }


# =============================================================================
# DETERMINE COMBINED RESULT AND Algo
# =============================================================================

def determine_result_code(ultimate, pasttel):
    """Determine the Result Code for the CSV row.

    Use Ultimate as ground truth if available, otherwise use pasttel.
    """
    if ultimate["result"] != "UNKNOWN":
        return ultimate["result"]
    if pasttel["result"] != "UNKNOWN":
        return pasttel["result"]
    return "UNKNOWN"


def ulr_baseline_ms(ultimate, result_code):
    """Sequential ULR time up to and including the winning strategy, or None.

    Ultimate stops at the first conclusive strategy and only logs what ran, so the
    sum of all logged times is the cumulative time whatever the order in which
    the strategies ran: strategies after the winner contribute 0.
    """
    if result_code not in ("TERMINATING", "NONTERMINATING"):
        return None
    return (ultimate["fixpoint_time_ms"] + ultimate["nontermination_time_ms"]
            + ultimate["termination_time_ms"])


def determine_algo(ultimate, pasttel):
    """Determine the Algo column: both algorithms separated by /."""
    u_algo = ultimate["algo"] if ultimate["algo"] != "-" else None
    t_algo = pasttel["algo"] if pasttel["algo"] != "-" else None

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

def parse_float(s):
    """Parse a float from a string that may use comma as decimal separator
    and/or be wrapped in quotes."""
    s = s.strip().strip('"').replace(",", ".")
    if s == "-" or s == "":
        return None
    try:
        return float(s)
    except ValueError:
        return None


PASTTEL_SUPPORTED_TERM_ALGOS = {
    "Affine Template",
    "Nested Template",
}

def _ultimate_algo_is_supported_by_pasttel(u_algo_raw):
    """Return True if the Ultimate termination algorithm is implemented by PaSTTeL.

    PaSTTeL has AffineTemplate, NestedTemplate, LexicographicTemplate,
    MultiphaseTemplate and PiecewiseTemplate. n-Nested/n-Phase/n-Lex (e.g.
    "4-nested", "2-phase") ARE supported since PaSTTeL's templates take a
    component-count range too.
    """
    name = u_algo_raw.strip().lower()
    # Strip optional numeric prefix: "4-nested" → base="nested"
    m = re.match(r'^(\d+)-(.+)$', name)
    base = m.group(2) if m else name
    # Also strip " template" suffix for already-normalised display labels
    base = re.sub(r'\s+template$', '', base).strip()
    return base in ("affine", "nested", "lex", "lexicographic", "phase", "piecewise")


_SOLVED = ("TERMINATING", "NONTERMINATING")


def _pulr_results(csv_path):
    """Read one benchmark CSV. Returns (label of its P-ULR column, ULR-Baseline results, P-ULR results,
    outcomes): a result is {trace: (verdict, ms)} over the traces that tool proved TERMINATING or
    NONTERMINATING, and outcomes is {trace: (Result Code, PaSTTeL Status)} over all the traces."""
    ulr, pulr, outcomes = {}, {}, {}
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        p_col = next((h for h in (reader.fieldnames or []) if h.startswith("P-ULR")), "P-ULR")
        for row in reader:
            name = row["Trace Name"].strip()
            code, status = row["Result Code"].strip(), row.get("PaSTTeL Status", "").strip() or "-"
            outcomes[name] = (code, status)
            ms = parse_float(row.get("ULR-Baseline (ms)", "-"))
            if code in _SOLVED and ms is not None:
                ulr[name] = (code, ms)
            ms = parse_float(row.get(p_col, "-"))
            if status in _SOLVED and ms is not None:
                pulr[name] = (status, ms)
    return p_col, ulr, pulr, outcomes


def _pulr_strategies(csv_path):
    """{trace: (ULR-Baseline's strategy, P-ULR's strategy)} from the Algo column, which reads
    "<ULR-Baseline> / <P-ULR>" (e.g. "2-Nested Template / 2-4-Nested Template", "Fixpoint / GNTA"),
    or "<ULR-Baseline>" alone when PaSTTeL did not prove the trace."""
    strategies = {}
    with open(csv_path, newline="") as f:
        for row in csv.DictReader(f):
            parts = [p.strip() for p in row.get("Algo", "").split("/")]
            strategies[row["Trace Name"].strip()] = (parts[0] or "-", parts[1] if len(parts) > 1 else "-")
    return strategies


_LEFT_OUT = ("INFEASIBLE", "UNCHECKED")


def pulr_table(csv_paths, baseline_name=None):
    """The paper's table for ULR-Baseline and the P-ULR configurations of csv_paths (one CSV each, all
    on the same traces), split by verdict. A trace counts for a tool that proved it TERMINATING or
    NONTERMINATING; timeouts, UNKNOWN and unsupported traces enter no total.
      tool rows  the traces every tool proved, each tool's time cumulated over exactly these traces;
      VBS        the virtual best solver: the traces any tool proved, each at the best tool's time.
    A trace two tools proved with opposite verdicts is a contradiction: left out of both, and listed.
    Returns (text lines, common traces {verdict: [trace]}, contradicted traces)."""
    tools, outcomes = [], None
    for i, path in enumerate(csv_paths):
        label, ulr, pulr, outc = _pulr_results(path)
        if i == 0:
            tools.append(("ULR-Baseline", ulr))
            outcomes = outc
        tools.append((label, pulr))
    vbs = {v: {} for v in _SOLVED}
    common = {v: [] for v in _SOLVED}
    contra = []
    for trace in sorted(set().union(*(res.keys() for _, res in tools))):
        verdicts = {res[trace][0] for _, res in tools if trace in res}
        if len(verdicts) > 1:
            contra.append(trace)
            continue
        verdict = verdicts.pop()
        vbs[verdict][trace] = min(res[trace][1] for _, res in tools if trace in res)
        if all(trace in res for _, res in tools):
            common[verdict].append(trace)
    secs = {label: {v: sum(res[t][1] for t in common[v]) / 1000.0 for v in _SOLVED} for label, res in tools}
    best = {v: min(secs[label][v] for label, _ in tools) for v in _SOLVED}
    vbs_secs = {v: sum(vbs[v].values()) / 1000.0 for v in _SOLVED}

    # The traces a tool did not prove, among those analysed (not INFEASIBLE or UNCHECKED), by outcome.
    analysed = [t for t, (code, _) in outcomes.items() if code not in _LEFT_OUT]
    left_out = collections.Counter(code for code, _ in outcomes.values() if code in _LEFT_OUT)
    not_proved = []
    for i, (label, res) in enumerate(tools):
        why = collections.Counter(
            (outcomes[t][0] if i == 0 else ("not run" if outcomes[t][1] == "-" else outcomes[t][1]))
            for t in analysed if t not in res)
        not_proved.append(f"{label} {sum(why.values())}"
                          + (" (" + ", ".join(f"{k} {n}" for k, n in why.most_common()) + ")" if why else ""))

    n_common = sum(len(common[v]) for v in _SOLVED)
    origin = f", ULR-Baseline from {baseline_name}" if baseline_name else ""
    w = max(len(label) for label, _ in tools)
    head = f"{'Tool':<{w}}  {'#Terminating':>12}  {'Time (s)':>10}   {'#Non-Terminating':>16}  {'Time (s)':>10}"
    sep = "-" * (len(head) + 1)
    lines = [f"ULR-Baseline vs P-ULR{origin}",
             f"{len(outcomes)} lasso traces, {len(analysed)} analysed"
             + (" (left out: " + ", ".join(f"{n} {k.lower()}" for k, n in left_out.most_common()) + ")"
                if left_out else ""),
             sep, head, sep,
             f"{'VBS':<{w}}  {len(vbs['TERMINATING']):>12}  {vbs_secs['TERMINATING']:>10.2f}    "
             f"{len(vbs['NONTERMINATING']):>16}  {vbs_secs['NONTERMINATING']:>10.2f}"]
    for label, _ in tools:
        cells = [(len(common[v]), f"{secs[label][v]:.2f}{'*' if secs[label][v] == best[v] else ' '}")
                 for v in _SOLVED]
        lines.append(f"{label:<{w}}  {cells[0][0]:>12}  {cells[0][1]:>11}   {cells[1][0]:>16}  {cells[1][1]:>11}")
    lines += [sep,
              f"Tools: the {n_common} traces every tool proved. VBS: the traces any tool proved, each at "
              "its best time. * best.",
              "Not proved: " + " | ".join(not_proved)]
    if contra:
        lines.append(f"CONTRADICTIONS ({len(contra)}), tools proved these with opposite verdicts:")
        lines += [f"  {t}" for t in contra]
    else:
        lines.append("Contradictions: none")
    return lines, common, contra


def scatter_path(csv_path):
    """Where the scatter plot of a benchmark CSV goes: next to it, <csv>_scatter.html."""
    return os.path.splitext(csv_path)[0] + "_scatter.html"


def generate_scatter_plot(csv_path, output_html, timeout_s=600, log_scale=False, x_col="ulr-baseline",
                          baseline_name=None):
    """Write the HTML scatter plot of one benchmark CSV, laid out as the paper's Figure 5: the paper's
    table for ULR-Baseline and this CSV's P-ULR configuration, then two panels, the traces both proved
    terminating (left, green) and non-terminating (right, blue), one + per trace (X: ULR-Baseline,
    Y: P-ULR), with the diagonal; a contradiction is a black star in the panel of ULR-Baseline's
    verdict. Hovering a point gives each tool's verdict, the strategy that proved it, and its time."""
    table, common, contra = pulr_table([csv_path], baseline_name)
    p_col, ulr, pulr, _ = _pulr_results(csv_path)
    strategy = _pulr_strategies(csv_path)
    baseline = f"ULR-Baseline ({baseline_name})" if baseline_name else "ULR-Baseline"
    plot_title = f"{baseline} vs {p_col}"

    x_axis_label, y_axis_label = "ULR-Baseline (ms)", f"{p_col} (ms)"
    # (panel title, verdict, colour); a contradiction goes to the panel of ULR-Baseline's verdict.
    panels = [("Terminating", "TERMINATING", "green"), ("Non-terminating", "NONTERMINATING", "blue")]
    contra_both = [t for t in contra if t in ulr and t in pulr]
    hover = lambda t: (f"{t}<br>ULR-Baseline: {ulr[t][0]} ({strategy[t][0]}), {ulr[t][1]:.1f} ms"
                       f"<br>{p_col}: {pulr[t][0]} ({strategy[t][1]}), {pulr[t][1]:.1f} ms")
    traces, layout, n_plotted = [], {}, 0
    axis = {"type": "log" if log_scale else "linear", "showgrid": True, "gridcolor": "#ddd",
            "zeroline": False, "mirror": True, "showline": True, "linecolor": "black", "ticks": "outside"}
    if log_scale:
        # One tick per decade, labelled 10^k, as in the paper.
        axis.update(dtick=1, exponentformat="power")
    for i, (title, verdict, colour) in enumerate(panels, 1):
        xa, ya = ("x", "y") if i == 1 else (f"x{i}", f"y{i}")
        groups = [("both", colour, "cross-thin", common[verdict]),
                  ("contradiction", "black", "star", [t for t in contra_both if ulr[t][0] == verdict])]
        vals = []
        for kind, col, symbol, names in groups:
            if not names:
                continue
            xs, ys = [ulr[t][1] for t in names], [pulr[t][1] for t in names]
            vals += [v for v in xs + ys if v > 0]
            n_plotted += len(names)
            traces.append({
                "x": xs, "y": ys, "xaxis": xa, "yaxis": ya, "mode": "markers", "type": "scatter",
                "hoverinfo": "text", "text": [hover(t) for t in names],
                "name": f"{title} ({len(names)})" if kind == "both" else f"Contradiction ({len(names)})",
                # Thin "+" like the paper's figure: a line-only marker, drawn by its outline.
                "marker": {"color": col, "symbol": symbol, "size": 11 if symbol == "star" else 7,
                           "line": {"width": 1.2, "color": col}},
            })
        # Each panel has its own range, the same on both axes, so that y = x is the diagonal.
        lo, hi = (min(vals) * 0.7, max(vals) * 1.4) if vals else (1, 10)
        traces.append({"x": [lo, hi], "y": [lo, hi], "xaxis": xa, "yaxis": ya, "mode": "lines",
                       "type": "scatter", "showlegend": False, "hoverinfo": "skip",
                       "line": {"color": "black", "width": 1.2, "dash": "dash"}})
        rng = [math.log10(lo), math.log10(hi)] if log_scale else [0, hi]
        domain = [0.0, 0.44] if i == 1 else [0.56, 1.0]
        layout[f"xaxis{'' if i == 1 else i}"] = dict(axis, domain=domain, range=rng, title={"text": x_axis_label},
                                                     anchor=ya, constrain="domain")
        layout[f"yaxis{'' if i == 1 else i}"] = dict(axis, range=rng, title={"text": y_axis_label}, anchor=xa,
                                                     scaleanchor=xa, scaleratio=1, constrain="domain")
        layout.setdefault("annotations", []).append(
            {"text": title, "xref": f"{xa} domain", "yref": f"{ya} domain", "x": 0.5, "y": 1.08,
             "showarrow": False, "font": {"size": 15}})
    if not n_plotted:
        print(f"No trace proved by both ULR-Baseline and {p_col} in {csv_path}: no plot.")
        return None
    layout.update({"hovermode": "closest", "plot_bgcolor": "white", "showlegend": True,
                   "legend": {"orientation": "h", "x": 0.5, "xanchor": "center", "y": -0.18},
                   "margin": {"l": 70, "r": 30, "t": 50, "b": 90}})

    # Embed Plotly JS for offline use (no CDN dependency)
    try:
        import plotly
        import os as _os
        _plotly_js = _os.path.join(_os.path.dirname(plotly.__file__), "package_data", "plotly.min.js")
        with open(_plotly_js) as _f:
            _plotly_script = f"<script>{_f.read()}</script>"
    except Exception:
        _plotly_script = '<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>'

    esc = lambda text: str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    table_txt = esc("\n".join(table))
    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{esc(plot_title)} - Scatter Plot</title>
{_plotly_script}
<style>
  body {{ font-family: Arial, sans-serif; margin: 20px; }}
  pre {{ font-size: 0.95em; }}
  #plot {{ width: 1100px; max-width: 100%; height: 560px; }}
</style>
</head>
<body>
<h2>{esc(plot_title)} &mdash; Computation Time Comparison</h2>
<pre>{table_txt}</pre>
<p style="font-size:0.85em; color:#555; max-width:1100px;">
  As Figure 5 of the paper: one + per lasso trace both tools proved, terminating on the left,
  non-terminating on the right, ULR-Baseline on the X axis and {esc(p_col)} on the Y axis. A trace
  proved by one tool only counts in VBS above. Hover a point for each tool's verdict, strategy and time.
</p>
<div id="plot"></div>
<script>
Plotly.newPlot('plot', {json.dumps(traces)}, {json.dumps(layout)});
</script>
</body>
</html>"""
    with open(output_html, "w") as f:
        f.write(html)
    return output_html


# =============================================================================
# MAIN PIPELINE
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark Ultimate LassoRanker vs PaSTTeL"
    )
    parser.add_argument(
        "--input-dir", default=None,
        help="Directory starting with lass*.txt files"
    )
    parser.add_argument(
        "--pasttel-bin", default=None,
        help="Path to the pasttel binary"
    )
    parser.add_argument(
        "--output", default="benchmark_results.csv",
        help="Output CSV file (default: benchmark_results.csv)"
    )
    parser.add_argument(
        "--cpus", type=int, default=1,
        help="Number of CPUs for pasttel (default: 1)"
    )
    parser.add_argument(
        "--timeout", type=int, default=600,
        help="Timeout in seconds for each pasttel run (default: 600)"
    )
    parser.add_argument(
        "--plot", nargs="?", const=True, default=False,
        metavar="CSV_FILE",
        help="Generate scatter plot. Use alone with a CSV file to only plot "
             "(skip benchmarking), or add to a benchmark run to plot after."
    )
    parser.add_argument(
        "--table", nargs="+", metavar="CSV",
        help="Print the paper's table (VBS, ULR-Baseline, one row per P-ULR configuration) from benchmark "
             "CSVs of the same traces, e.g. the P-ULR-Seq and P-ULR-Par7 ones, and write the scatter plot "
             "of each CSV next to it"
    )
    parser.add_argument(
        "--baseline-name", default=None, metavar="NAME",
        help="Ultimate release the traces were extracted with (e.g. UAutomizer-linux); "
             "shown on the X axis and in the title of the scatter plot"
    )
    parser.add_argument(
        "--log", action="store_true", default=False,
        help="Use logarithmic scale for the scatter plot axes"
    )
    parser.add_argument(
        "--strat", choices=["terminate", "nonterminate", "both"], default="terminate",
        help="Analysis strategy passed to pasttel: 'terminate', 'nonterminate' or 'both' (default: terminate)"
    )
    parser.add_argument(
        "--solver", choices=["z3", "cvc5"], default="z3",
        help="Use specific SMT solver: 'z3' or 'cvc5' (default: z3)"
    )
    parser.add_argument(
        "--check", choices=["loop", "lasso"], default="lasso",
        help="Which Ultimate result field to use: 'loop' uses Loop termination, "
             "'lasso' uses Lasso termination (default: lasso)"
    )
    parser.add_argument(
        "--parse", choices=["normal", "preprocess"], default="normal",
        help="Which trace section to parse from .txt files: "
             "'normal' parses LINEARIZED TRACE (not fully linearized), "
             "'preprocess' parses PREPROCESSED LINEAR TRACE (fully linearized by Ultimate) "
             "(default: normal)"
    )
    args = parser.parse_args()

    # Table-only mode: --table CSV [CSV ...] (no benchmarking)
    if args.table:
        missing = [p for p in args.table if not os.path.isfile(p)]
        if missing:
            print(f"Error: CSV file not found: {', '.join(missing)}")
            sys.exit(1)
        print("\n".join(pulr_table(args.table, args.baseline_name)[0]))
        for csv_file in args.table:
            html = generate_scatter_plot(csv_file, scatter_path(csv_file), timeout_s=args.timeout,
                                         log_scale=args.log, baseline_name=args.baseline_name)
            if html:
                print(f"Scatter plot: {html}")
        for csv_file in args.table:
            print(f"CSV: {csv_file}")
        return

    # Plot-only mode: --plot CSV_FILE (no benchmarking)
    if isinstance(args.plot, str):
        csv_file = args.plot
        if not os.path.isfile(csv_file):
            print(f"Error: CSV file not found: {csv_file}")
            sys.exit(1)
        print("\n".join(pulr_table([csv_file], args.baseline_name)[0]))
        html = generate_scatter_plot(csv_file, scatter_path(csv_file), timeout_s=args.timeout,
                                     log_scale=args.log, baseline_name=args.baseline_name)
        if html:
            print(f"Scatter plot: {html}")
        return

    # Benchmark mode requires --input-dir and --pasttel-bin
    if not args.input_dir or not args.pasttel_bin:
        parser.error("--input-dir and --pasttel-bin are required for benchmarking")

    # Find all trace files
    pattern = os.path.join(args.input_dir, "lass*.txt")
    trace_files = sorted(glob.glob(pattern))
    # Ultimate dumps each synthesis scope to its own file: lasso_trace_N.txt for the
    # whole lasso, lasso_trace_N_loop.txt for the loop alone, whose RESULT reports the
    # lasso as UNCHECKED. Under --check lasso such files carry no lasso verdict and
    # would only add rows without a ULR baseline, so leave them out.
    if args.check == "lasso":
        trace_files = [f for f in trace_files if not f.endswith("_loop.txt")]

    if not trace_files:
        print(f"No lass*.txt files found in {args.input_dir}")
        sys.exit(1)

    print(f"Found {len(trace_files)} trace file(s) in {args.input_dir}")

    # Check pasttel binary exists
    if not os.path.isfile(args.pasttel_bin):
        print(f"Error: pasttel binary not found at {args.pasttel_bin}")
        sys.exit(1)

    # Create a directory next to the output CSV to store converted JSON files
    output_dir = os.path.dirname(os.path.abspath(args.output))
    #json_dir = os.path.join(output_dir, "json_traces")
    #os.makedirs(json_dir, exist_ok=True)
    #print(f"JSON files will be saved in: {json_dir}")

    results = []

    def fmt_ms(val):
        """Format a millisecond value, returning '-' if negative."""
        if isinstance(val, str):
            return val  # already formatted
        return f"{val:.2f}" if val >= 0 else "-"
        
 
    for trace_file in trace_files:
        basename = os.path.basename(trace_file)
        dirname=os.path.dirname(trace_file)
        
        print("** OUTPUT name ",dirname)
        print(f"\n{'='*60}")
        print(f"Processing: {basename}")
        print(f"{'='*60}")

        # 1. Parse Ultimate trace
        try:
            ultimate = parse_ultimate_trace(trace_file, check_mode=args.check, parse_mode=args.parse)
        except Exception as e:
            print(f"  ERROR parsing Ultimate trace: {e}")
            p_ulr_col = "P-ULR-Seq" if args.cpus == 1 else f"P-ULR-Par{args.cpus}"
            results.append({
                "Trace Name":          trace_file,
                "Result Code":         "UNKNOWN",
                "Fixpoint (ms)":       "-",
                "Termination (ms)":    "-",
                "Nontermination (ms)": "-",
                "ULR-Baseline (ms)":   "-",
                p_ulr_col:             "-",
                "PaSTTeL-TOTAL":       "-",
                "Stem Size":           0,
                "Loop Size":           0,
                "Total Size Trace":    0,
                "Algo":                "-",
            })
            continue

        print(f"  Ultimate result: {ultimate['result']}")
        print(f"  Ultimate algo:   {ultimate['algo']}")
        print(f"  Trace size:      {ultimate['size']} transition(s)")

        p_ulr_col = "P-ULR-Seq" if args.cpus == 1 else f"P-ULR-Par{args.cpus}"

        def _skip_row(res_code):
            return {
                "Trace Name":          trace_file,
                "Result Code":         res_code,
                "Fixpoint (ms)":       fmt_ms(ultimate['fixpoint_time_ms']),
                "Termination (ms)":    fmt_ms(ultimate['termination_time_ms']),
                "Nontermination (ms)": fmt_ms(ultimate['nontermination_time_ms']),
                "ULR-Baseline (ms)":   "-",
                p_ulr_col:             "-",
                "PaSTTeL-TOTAL":       "-",
                "PaSTTeL Status":      "-",
                "Stem Size":           ultimate['stem_size'],
                "Loop Size":           ultimate['loop_size'],
                "Total Size Trace":    ultimate['stem_size'] + ultimate['loop_size'],
                "Algo":                ultimate['algo'],
            }

        if ultimate['size'] == 0:
            print("  Skipping pasttel run due to zero-size trace.")
            results.append(_skip_row(ultimate['result']))
            continue
        if ultimate['result'] in ("UNCHECKED", "UNKNOWN"):
            print("  Skipping pasttel run due to unchecked or unknown trace.")
            results.append(_skip_row(ultimate['result']))
            continue
        if ultimate['result'] == "INFEASIBLE":
            print("  Skipping pasttel run due to infeasible trace.")
            results.append(_skip_row(ultimate['result']))
            continue

        # 2. Convert to JSON and save permanently
        json_data = convert_to_json(ultimate)

        json_name = os.path.splitext(basename)[0] + ".json"
        print("*** JSON name ",json_name)
        json_path = os.path.join(dirname, json_name)
        with open(json_path, "w") as jf:
            json.dump(json_data, jf, indent=2)

        print(f"  JSON written to: {json_path}")

        # 3. Run pasttel
        print(f"  Running pasttel (--strat {args.strat} -c {args.cpus})...")
        pasttel = run_pasttel(
            json_path, args.pasttel_bin, cpus=args.cpus, timeout_s=args.timeout,
            strat=args.strat, solver=args.solver
        )

        print(f"  PaSTTeL result:    {pasttel['result']}")
        print(f"  PaSTTeL P-ULR:     {pasttel['ulr_time_ms']:.2f} ms")
        print(f"  PaSTTeL TOTAL:     {pasttel['total_time_ms']:.2f} ms")
        print(f"  PaSTTeL algo:      {pasttel['algo']}")
        if "error" in pasttel:
            print(f"  PaSTTeL error:     {pasttel['error']}")

        # 4. Build CSV row
        result_code = determine_result_code(ultimate, pasttel)
        algo = determine_algo(ultimate, pasttel)

        t_ulr_time   = fmt_ms(pasttel['ulr_time_ms'])   if pasttel['ulr_time_ms']   >= 0 else "-"
        t_total_time = fmt_ms(pasttel['total_time_ms']) if pasttel['total_time_ms'] >= 0 else "-"

        # P-ULR column name: P-ULR-Seq (cpus=1) or P-ULR-Par{N} (cpus>1)
        p_ulr_col = "P-ULR-Seq" if args.cpus == 1 else f"P-ULR-Par{args.cpus}"

        # PaSTTeL Status
        u_algo_supported = _ultimate_algo_is_supported_by_pasttel(ultimate["algo"])
        if pasttel.get("error") == "TIMEOUT":
            p_status = "TIMEOUT"
        elif pasttel["result"] == "NOT SUPPORTED":
            p_status = "NOT_SUPPORTED"
        elif pasttel["result"] == "TERMINATING":
            p_status = "TERMINATING"
        elif pasttel["result"] == "NONTERMINATING":
            p_status = "NONTERMINATING"
        elif (ultimate["result"] == "TERMINATING"
              and not u_algo_supported
              and pasttel["result"] == "UNKNOWN"):
            p_status = "NOT_SUPPORTED"
        else:
            p_status = "UNKNOWN"

        # ULR-Baseline: cumulative sequential ULR time up to (and including) the winner.
        baseline_ms = ulr_baseline_ms(ultimate, result_code)

        row = {
            "Trace Name":          trace_file,
            "Result Code":         result_code,
            "Fixpoint (ms)":       fmt_ms(ultimate['fixpoint_time_ms']),
            "Termination (ms)":    fmt_ms(ultimate['termination_time_ms']),
            "Nontermination (ms)": fmt_ms(ultimate['nontermination_time_ms']),
            "ULR-Baseline (ms)":   fmt_ms(baseline_ms) if baseline_ms is not None else "-",
            p_ulr_col:             t_ulr_time,
            "PaSTTeL-TOTAL":       t_total_time,
            "PaSTTeL Status":      p_status,
            "Stem Size":           ultimate["stem_size"],
            "Loop Size":           ultimate["loop_size"],
            "Total Size Trace":    ultimate["stem_size"] + ultimate["loop_size"],
            "Algo":                algo,
        }
        results.append(row)

    # 5. Write CSV
    if results:
        p_ulr_col = "P-ULR-Seq" if args.cpus == 1 else f"P-ULR-Par{args.cpus}"
        fieldnames = [
            "Trace Name",
            "Result Code",
            "Fixpoint (ms)",
            "Termination (ms)",
            "Nontermination (ms)",
            "ULR-Baseline (ms)",
            p_ulr_col,
            "PaSTTeL-TOTAL",
            "PaSTTeL Status",
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
        print(f"\n{'Trace Name':<70} {'Result Code':<15} {'Fixpoint (ms)':<15} {'Termination (ms)':<18} {'Nontermination (ms)':<21} {'ULR-Baseline (ms)':<20} {p_ulr_col:<18} {'PaSTTeL-TOTAL':<16} {'Stem':<6} {'Loop':<6} {'Total':<7} {'Algo'}")
        print("-" * 235)
        for row in results:
            print(
                f"{row['Trace Name']:<70} "
                f"{row['Result Code']:<15} "
                f"{str(row['Fixpoint (ms)']):<15} "
                f"{str(row['Termination (ms)']):<18} "
                f"{str(row['Nontermination (ms)']):<21} "
                f"{str(row.get('ULR-Baseline (ms)', '-')):<20} "
                f"{str(row.get(p_ulr_col, '-')):<18} "
                f"{str(row['PaSTTeL-TOTAL']):<16} "
                f"{str(row['Stem Size']):<6} "
                f"{str(row['Loop Size']):<6} "
                f"{str(row['Total Size Trace']):<7} "
                f"{row['Algo']}"
            )

        # Generate scatter plot if requested
        if args.plot:
            print("\n".join(pulr_table([args.output], args.baseline_name)[0]))
            html = generate_scatter_plot(args.output, scatter_path(args.output), timeout_s=args.timeout,
                                         log_scale=args.log, baseline_name=args.baseline_name)
            if html:
                print(f"Scatter plot: {html}")


if __name__ == "__main__":
    main()
