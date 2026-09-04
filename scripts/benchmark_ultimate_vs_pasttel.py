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
    cmd = [pasttel_bin, "-a", strat, "-c", str(cpus), "-s", solver, json_path]

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


def generate_scatter_plot(csv_path, output_html, timeout_s=600, log_scale=False, x_col="ulr-baseline"):
    """Read the benchmark CSV and generate an interactive HTML scatter plot.

    X axis: ULR-Baseline (ms)  — cumulative sequential ULR time
    Y axis: P-ULR (ms)         — P-ULR column from PaSTTeL

    Points are colored:
      - Green:  both agree TERMINATING
      - Blue:   both agree NONTERMINATING
      - Orange: PaSTTeL timeout
      - Red:    UNKNOWN (Ultimate answered, PaSTTeL did not)
      - Purple: NOT SUPPORTED by PaSTTeL

    When PaSTTeL times out or is not supported, a PAR-2 penalty time
    (timeout * 2) is used on the Y axis.
    Rows with INFEASIBLE / UNCHECKED / Ultimate-UNKNOWN are skipped.
    """
    x_axis_label = "ULR-Baseline (ms)"
    # Derive Y-axis label from the CSV header (P-ULR-Seq or P-ULR-Par*)
    y_axis_label = "P-ULR (ms)"
    plot_title   = "ULR-Baseline vs P-ULR"

    par2_ms = timeout_s * 2 * 1000.0
    rows = []
    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or []
        # Auto-detect the P-ULR column name (P-ULR-Seq, P-ULR-Par4, …)
        p_ulr_col = next((h for h in fieldnames if h.startswith("P-ULR")), "P-ULR")
        if p_ulr_col != "P-ULR":
            y_axis_label = f"{p_ulr_col} (ms)"
            plot_title   = f"ULR-Baseline vs {p_ulr_col}"
        for row in reader:
            rows.append(row)

    green_x, green_y, green_labels = [], [], []
    blue_x, blue_y, blue_labels = [], [], []
    red_x, red_y, red_labels = [], [], []
    orange_x, orange_y, orange_labels = [], [], []
    purple_x, purple_y, purple_labels = [], [], []
    # Contradiction: BOTH tools concluded, but disagree (one says TERMINATING,
    # the other NONTERMINATING). This is a soundness discrepancy, not a mere
    # PaSTTeL failure, so it gets its own category and colour.
    contra_x, contra_y, contra_labels = [], [], []

    def verdict_from_algo(a):
        a = a.strip().lower()
        if a in ("fixpoint", "gnta"):
            return "NONTERMINATING"
        if "template" in a:
            return "TERMINATING"
        return "UNKNOWN"

    col_name = "Baseline"

    for row in rows:
        result = row["Result Code"].strip()
        t_time_str = row.get(p_ulr_col, "-").strip()
        name = row["Trace Name"].strip()
        algo = row.get("Algo", "").strip()
        pasttel_status = row.get("PaSTTeL Status", "").strip()

        algo_parts = [p.strip() for p in algo.split("/")] if "/" in algo else [algo]
        u_algo_raw = algo_parts[0].strip()

        u_time_str = row.get("ULR-Baseline (ms)", "-").strip()

        if result in ("INFEASIBLE", "UNCHECKED", "UNKNOWN"):
            continue
        if u_time_str.strip().strip('"') in ("-", ""):
            continue

        u_verdict = result

        t_algo = algo_parts[-1] if len(algo_parts) >= 2 else ""
        t_verdict = verdict_from_algo(t_algo) if t_algo else "UNKNOWN"
        if pasttel_status in ("TERMINATING", "NONTERMINATING"):
            t_verdict = pasttel_status

        if pasttel_status:
            p_status = pasttel_status
        elif t_time_str.strip().strip('"') == "-":
            p_status = "UNKNOWN"
        else:
            p_status = t_verdict

        ux = parse_float(u_time_str)
        ty_raw = parse_float(t_time_str)

        if ux is None:
            continue
        ty = ty_raw if ty_raw is not None else par2_ms

        u_algo_supported = _ultimate_algo_is_supported_by_pasttel(u_algo_raw)
        is_not_supported = (
            u_verdict == "TERMINATING"
            and not u_algo_supported
            and p_status not in ("TERMINATING", "NONTERMINATING")
        )

        if p_status == "NOT_SUPPORTED" or is_not_supported:
            purple_x.append(ux); purple_y.append(ty)
            purple_labels.append(
                f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms"
                f"<br>Ultimate: {u_verdict} ({u_algo_raw}), PaSTTeL: NOT SUPPORTED"
            )
        elif u_verdict == "TERMINATING" and t_verdict == "TERMINATING":
            green_x.append(ux); green_y.append(ty)
            green_labels.append(f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms  P-ULR={ty:.1f}ms")
        elif u_verdict == "NONTERMINATING" and t_verdict == "NONTERMINATING":
            blue_x.append(ux); blue_y.append(ty)
            blue_labels.append(f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms  P-ULR={ty:.1f}ms")
        elif t_verdict in ("TERMINATING", "NONTERMINATING") and t_verdict != u_verdict:
            # Both concluded but disagree -> soundness contradiction.
            contra_x.append(ux); contra_y.append(ty)
            contra_labels.append(
                f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms  P-ULR={ty:.1f}ms"
                f"<br>CONTRADICTION — Ultimate: {u_verdict} ({u_algo_raw}), PaSTTeL: {t_verdict}"
            )
        elif p_status == "TIMEOUT":
            # Real timeout only (subprocess killed at the time limit). A genuine
            # UNKNOWN concluded *under* the timeout has ty_raw is None too, but must
            # NOT be coloured orange — it falls through to the red (UNKNOWN) branch.
            orange_x.append(ux); orange_y.append(ty)
            orange_labels.append(
                f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms"
                f"<br>Ultimate: {u_verdict}, PaSTTeL: TIMEOUT (PAR-2={par2_ms:.0f}ms)"
            )
        elif u_verdict == "TERMINATING" and t_verdict != "TERMINATING":
            red_x.append(ux); red_y.append(ty)
            red_labels.append(
                f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms  P-ULR={ty:.1f}ms"
                f"<br>Ultimate: TERMINATING ({u_algo_raw}), PaSTTeL: {t_verdict}"
            )
        else:
            red_x.append(ux); red_y.append(ty)
            red_labels.append(
                f"{name}<br>Algo: {algo}<br>ULR-Baseline={ux:.1f}ms  P-ULR={ty:.1f}ms"
                f"<br>Ultimate: {u_verdict}, PaSTTeL: {t_verdict}"
            )

    all_y = green_y + blue_y + red_y + orange_y + purple_y + contra_y
    all_x = green_x + blue_x + red_x + orange_x + purple_x + contra_x
    if not all_x:
        print("No plottable data points found (all INFEASIBLE/UNCHECKED or missing times).")
        return

    x_ref = "ULR-Baseline"
    y_ref = p_ulr_col

    # ── Summary table ────────────────────────────────────────────────────────
    n_term        = len(green_x)
    n_nonterm     = len(blue_x)
    n_timeout     = len(orange_x)
    n_unknown     = len(red_x)
    n_notsup      = len(purple_x)
    n_contra      = len(contra_x)
    total_term_x    = sum(green_x)   / 1000.0
    total_term_y    = sum(green_y)   / 1000.0
    total_nonterm_x = sum(blue_x)    / 1000.0
    total_nonterm_y = sum(blue_y)    / 1000.0
    total_contra_x  = sum(contra_x)  / 1000.0
    total_contra_y  = sum(contra_y)  / 1000.0

    # PAR-2: an instance PaSTTeL did not solve (timeout / unknown / not-supported)
    # is penalised as 2*timeout on the P-ULR (Y) axis. Those Y values already hold
    # par2_ms (substituted when ty_raw is None), so summing them yields the
    # penalised cumulative runtime. Ultimate (X) solved every plotted instance,
    # so its cumulative carries no penalty.
    total_timeout_x = sum(orange_x) / 1000.0
    total_timeout_y = sum(orange_y) / 1000.0
    total_unknown_x = sum(red_x)    / 1000.0
    total_unknown_y = sum(red_y)    / 1000.0
    total_notsup_x  = sum(purple_x) / 1000.0
    total_notsup_y  = sum(purple_y) / 1000.0

    # Instances solved by BOTH techniques (common terminating + non-terminating).
    n_both       = n_term + n_nonterm
    total_both_x = total_term_x + total_nonterm_x
    total_both_y = total_term_y + total_nonterm_y

    # PAR-2 grand total over the instances that count: solved (terminating +
    # non-terminating) and genuine timeouts. Unknown (red) and Not-supported
    # (purple) instances are excluded entirely. Timeout Y values already hold
    # par2_ms (the 2*timeout penalty); solved Y values hold real times.
    par2_x = green_x + blue_x + orange_x
    par2_y = green_y + blue_y + orange_y
    n_all        = len(par2_x)
    par2_total_x = sum(par2_x) / 1000.0
    par2_total_y = sum(par2_y) / 1000.0

    x_col_hdr = x_axis_label   # e.g. "ULR-Fair (ms)" or "ULR-Baseline (ms)"
    y_col_hdr = y_axis_label   # e.g. "P-ULR-Seq (ms)" or "P-ULR-Par6 (ms)"

    # NB: the X column ("ULR-Baseline") is always the reference tool's own time.
    # It ALWAYS concludes TERMINATING/NONTERMINATING on every plotted instance
    # (INFEASIBLE/UNCHECKED/UNKNOWN rows are filtered out upstream). The
    # "Timeout / Unknown / Not supported" categories therefore describe PaSTTeL's
    # outcome, NOT the reference — the X time there is simply how long the
    # reference took to solve the instances PaSTTeL could not.
    hdr = f"{'Category':<34}  {'Count':>6}  {x_col_hdr:>22}  {y_col_hdr:>22}"
    sep = "-" * len(hdr)
    rows_txt = [
        f"{'Terminating (common)':<34}  {n_term:>6}  {total_term_x:>19.2f} s  {total_term_y:>19.2f} s",
        f"{'Non-terminating (common)':<34}  {n_nonterm:>6}  {total_nonterm_x:>19.2f} s  {total_nonterm_y:>19.2f} s",
        f"{'Contradiction (both disagree)':<34}  {n_contra:>6}  {total_contra_x:>19.2f} s  {total_contra_y:>19.2f} s",
        f"{'PaSTTeL Timeout (PAR-2 x2)':<34}  {n_timeout:>6}  {total_timeout_x:>19.2f} s  {total_timeout_y:>19.2f} s",
        f"{'PaSTTeL Unknown (no P-ULR time)':<34}  {n_unknown:>6}  {total_unknown_x:>19.2f} s  {'-':>21}",
        f"{'PaSTTeL Not supported (no P-ULR)':<34}  {n_notsup:>6}  {total_notsup_x:>19.2f} s  {'-':>21}",
    ]
    print(f"\n{sep}\n{hdr}\n{sep}")
    for r in rows_txt:
        print(r)
    print(sep)
    print(f"{'Solved by BOTH (cumul.)':<34}  {n_both:>6}  {total_both_x:>19.2f} s  {total_both_y:>19.2f} s")
    print(f"{'PAR-2 total (all)':<34}  {n_all:>6}  {par2_total_x:>19.2f} s  {par2_total_y:>19.2f} s")
    print(sep + "\n")

    summary_html = f"""
<h3>Summary</h3>
<table border="1" cellpadding="6" cellspacing="0"
       style="border-collapse:collapse; font-family:monospace; margin-bottom:20px;">
<thead style="background:#f0f0f0;">
  <tr>
    <th>Category</th><th>Count</th>
    <th>{x_col_hdr} total (s)</th>
    <th>{y_col_hdr} total (s)</th>
  </tr>
</thead>
<tbody>
  <tr style="color:green;">
    <td>Terminating (common)</td>
    <td style="text-align:right;">{n_term}</td>
    <td style="text-align:right;">{total_term_x:.2f}</td>
    <td style="text-align:right;">{total_term_y:.2f}</td>
  </tr>
  <tr style="color:blue;">
    <td>Non-terminating (common)</td>
    <td style="text-align:right;">{n_nonterm}</td>
    <td style="text-align:right;">{total_nonterm_x:.2f}</td>
    <td style="text-align:right;">{total_nonterm_y:.2f}</td>
  </tr>
  <tr style="color:black; background:#fff3cd;">
    <td>Contradiction (both disagree)</td>
    <td style="text-align:right;">{n_contra}</td>
    <td style="text-align:right;">{total_contra_x:.2f}</td><td style="text-align:right;">{total_contra_y:.2f}</td>
  </tr>
  <tr style="color:orange;">
    <td>{y_ref} Timeout (PAR-2 &times;2)</td>
    <td style="text-align:right;">{n_timeout}</td>
    <td style="text-align:right;">{total_timeout_x:.2f}</td><td style="text-align:right;">{total_timeout_y:.2f}</td>
  </tr>
  <tr style="color:red;">
    <td>{y_ref} Unknown (no P-ULR time)</td>
    <td style="text-align:right;">{n_unknown}</td>
    <td style="text-align:right;">{total_unknown_x:.2f}</td><td style="text-align:right;">-</td>
  </tr>
  <tr style="color:purple;">
    <td>{y_ref} Not supported (no P-ULR time)</td>
    <td style="text-align:right;">{n_notsup}</td>
    <td style="text-align:right;">{total_notsup_x:.2f}</td><td style="text-align:right;">-</td>
  </tr>
  <tr style="font-weight:bold; border-top:2px solid #333;">
    <td>Solved by both (cumulative)</td>
    <td style="text-align:right;">{n_both}</td>
    <td style="text-align:right;">{total_both_x:.2f}</td><td style="text-align:right;">{total_both_y:.2f}</td>
  </tr>
  <tr style="font-weight:bold;">
    <td>PAR-2 total (all instances)</td>
    <td style="text-align:right;">{n_all}</td>
    <td style="text-align:right;">{par2_total_x:.2f}</td><td style="text-align:right;">{par2_total_y:.2f}</td>
  </tr>
</tbody>
</table>"""

    max_val = max(max(all_x), max(all_y)) if all_y else max(all_x)

    # Embed Plotly JS for offline use (no CDN dependency)
    try:
        import plotly
        import os as _os
        _plotly_js = _os.path.join(_os.path.dirname(plotly.__file__), "package_data", "plotly.min.js")
        with open(_plotly_js) as _f:
            _plotly_bundle = _f.read()
        _plotly_script = f"<script>{_plotly_bundle}</script>"
    except Exception:
        _plotly_script = '<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>'

    # Build the HTML with Plotly
    html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>{plot_title} - Scatter Plot</title>
{_plotly_script}
<style>
  body {{ font-family: Arial, sans-serif; margin: 20px; }}
  #plot {{ width: 100%; height: 85vh; }}
</style>
</head>
<body>
<h2>{plot_title} &mdash; Computation Time Comparison</h2>
<p>
  <span style="color:green;">&#9679;</span> Terminating &nbsp;
  <span style="color:blue;">&#9679;</span> Non-terminating &nbsp;
  <span style="color:black;">&#9733;</span> Contradiction &nbsp;
  <span style="color:orange;">&#9679;</span> {y_ref} Timeout &nbsp;
  <span style="color:red;">&#9679;</span> {y_ref} Unknown &nbsp;
  <span style="color:purple;">&#9679;</span> {y_ref} Not supported
</p>
<p style="font-size:0.85em; color:#555;">
  Note: the X axis ({x_ref}) is the reference tool and always concludes
  TERMINATING/NONTERMINATING on every plotted instance. The Timeout / Unknown /
  Not-supported categories describe {y_ref}'s outcome — the X time shown there is
  the reference's own time to solve instances {y_ref} could not.
</p>
{summary_html}
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
var orange = {{
  x: {json.dumps(orange_x)},
  y: {json.dumps(orange_y)},
  text: {json.dumps(orange_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Timeout {y_ref} ({len(orange_x)})',
  marker: {{ color: 'orange', size: 9, opacity: 0.85, symbol: 'circle-open' }},
  hoverinfo: 'text'
}};
var red = {{
  x: {json.dumps(red_x)},
  y: {json.dumps(red_y)},
  text: {json.dumps(red_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Unknown {x_ref}=TERM ({len(red_x)})',
  marker: {{ color: 'red', size: 10, opacity: 0.85, symbol: 'x' }},
  hoverinfo: 'text'
}};
var purple = {{
  x: {json.dumps(purple_x)},
  y: {json.dumps(purple_y)},
  text: {json.dumps(purple_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Not supported by {y_ref} ({len(purple_x)})',
  marker: {{ color: 'purple', size: 9, opacity: 0.85, symbol: 'diamond-open' }},
  hoverinfo: 'text'
}};
var contra = {{
  x: {json.dumps(contra_x)},
  y: {json.dumps(contra_y)},
  text: {json.dumps(contra_labels)},
  mode: 'markers',
  type: 'scatter',
  name: 'Contradiction ({len(contra_x)})',
  marker: {{ color: 'black', size: 12, opacity: 0.9, symbol: 'star' }},
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
    title: '{x_axis_label}',
    {"type: 'log'," if log_scale else "rangemode: 'tozero',"}
  }},
  yaxis: {{
    title: '{y_axis_label}',
    {"type: 'log'," if log_scale else "rangemode: 'tozero',"}
  }},
  hovermode: 'closest',
  legend: {{ x: 0.01, y: 0.99, bgcolor: 'rgba(255,255,255,0.8)' }},
  margin: {{ l: 70, r: 30, t: 30, b: 70 }}
}};
Plotly.newPlot('plot', [diagonal, green, blue, orange, red, purple, contra], layout);
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

    # Plot-only mode: --plot CSV_FILE (no benchmarking)
    if isinstance(args.plot, str):
        csv_file = args.plot
        if not os.path.isfile(csv_file):
            print(f"Error: CSV file not found: {csv_file}")
            sys.exit(1)
        html_out = os.path.splitext(csv_file)[0] + "_scatter.html"
        generate_scatter_plot(csv_file, html_out, timeout_s=args.timeout, log_scale=args.log)
        return

    # Benchmark mode requires --input-dir and --pasttel-bin
    if not args.input_dir or not args.pasttel_bin:
        parser.error("--input-dir and --pasttel-bin are required for benchmarking")

    # Find all trace files
    pattern = os.path.join(args.input_dir, "lass*.txt")
    trace_files = sorted(glob.glob(pattern))

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
        # Order: Fixpoint → Nontermination → Termination
        u_algo_lower = ultimate["algo"].strip().lower()
        fix_ms   = ultimate['fixpoint_time_ms']
        term_ms  = ultimate['termination_time_ms']
        nonterm_ms = ultimate['nontermination_time_ms']
        if result_code == "NONTERMINATING" and "fixpoint" in u_algo_lower:
            baseline_ms = fix_ms
        elif result_code == "NONTERMINATING":
            baseline_ms = fix_ms + nonterm_ms
        elif result_code == "TERMINATING":
            baseline_ms = fix_ms + nonterm_ms + term_ms
        else:
            baseline_ms = None

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
            html_out = os.path.splitext(args.output)[0] + "_scatter.html"
            generate_scatter_plot(args.output, html_out, timeout_s=args.timeout, log_scale=args.log)


if __name__ == "__main__":
    main()
