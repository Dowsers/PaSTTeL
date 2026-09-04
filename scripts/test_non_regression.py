#!/usr/bin/env python3
"""
Non-regression test suite for PaSTTeL.
Produces JUnit XML output readable by Jenkins.

To add a test case, append a tuple to CASES:
    ("path/to/file.json", "EXPECTED_RESULT", "mode")         # cpus defaults to 1
    ("path/to/file.json", "EXPECTED_RESULT", "mode", cpus)
"""

import os
import re
import subprocess
import sys
import unittest

PASTTEL_BIN = os.environ.get("PASTTEL_BIN", "./bin/pasttel")
CPUS=3

# ---------------------------------------------------------------------------
# Test cases — format: (file, expected_result, mode[, cpus])
# ---------------------------------------------------------------------------

CASES = [
    ("examples/test_simple_counter.json",                               "TERMINATING",     "both"),
    ("examples/test_simple_counter_real.json",                          "TERMINATING",     "both"),
    ("examples/test_variable_decrease.json",                            "TERMINATING",     "both"),
    ("examples/test_ranking_func_with_two_variables.json",              "TERMINATING",     "both"),
    ("examples/multiplication_termination.json",                        "TERMINATING",     "both"),
    ("examples/test_unbounded_counter.json",                            "NON-TERMINATING", "both"),
  # ("examples/test_geometric_doubling.json",                           "NON-TERMINATING", "both"),
    ("examples/nonterminate_booleans.json",                             "NON-TERMINATING", "both"),
    ("examples/test_ranking_func_with_two_variables_non_terminating.json", "NON-TERMINATING", "both"),
    ("examples/test_with_div_mod.json",                                 "TERMINATING",     "both",        CPUS),
    ("examples/test_with_div_mod_mult.json",                            "TERMINATING",     "both",        CPUS),
    ("examples/test_division_termination.json",                         "TERMINATING",     "both",        CPUS),
    ("examples/test_nested_template_terminating.json",                  "TERMINATING",     "both",        CPUS),
    ("examples/multiplication_termination.json",                        "TERMINATING",     "both",        CPUS),
    ("examples/nonterminate_booleans.json",                             "NON-TERMINATING", "both",        CPUS),
    ("examples/fixpoint_nontermination.json",                           "NON-TERMINATING", "both",        CPUS),
    ("examples/test_hash_function_axioms.json",                         "TERMINATING",     "both"),
    ("examples/test_array_sum_axioms.json",                             "TERMINATING",     "both"),
    ("examples/test_token_transfer_axioms.json",                        "TERMINATING",     "both"),
    ("examples/test_mapping_axioms.json",                               "TERMINATING",     "both"),
    ("examples/test_infinite_loop_with_axioms.json",                    "NON-TERMINATING", "both"),
    ("examples/test_erc20_simple.json",                                 "TERMINATING",     "both"),
    ("examples/test_array_select_simple.json",                          "TERMINATING",     "both"),
    ("examples/test_lexicographic_simple.json",                         "TERMINATING",     "both"),
    ("examples/test_stem_si_phi1.json",                                 "TERMINATING",     "both"),
    ("examples/test_fun2Bt2_affine.json",                               "TERMINATING",     "both"),
    ("examples/test_polyrank4t2_nested3.json",                          "TERMINATING",     "both"),
    ("examples/ref_rational_and_simplification.json",                   "TERMINATING",     "both"),
    ("examples/test_nonterminate_gnta_real.json",                       "NON-TERMINATING", "both"),
    ("examples/test_nonterminate_fixpoint_real.json",                   "NON-TERMINATING", "both"),
    ("examples/BugOldVars03_1.json",                   			"TERMINATING",     "both",        CPUS),
    ("examples/only_termination_Ackermann_true-termination1_affine.json","TERMINATING",    "both",        CPUS),
    ("examples/noInlineTest_nonterminate_GNTA.json",			"NON-TERMINATING", "both", 	  CPUS),
    ("examples/terminate_in_out_ssa_inconsistency_CountTillBound.json", "TERMINATING",     "both",        CPUS),    
    ("examples/unused_variables_tqli.t2.json", 				"TERMINATING",     "both",        CPUS),    
    ("examples/unused_variables_ChenFlurMukhopadhyay-SAS2012-Ex2.22.json","TERMINATING",   "both",        CPUS),    
    ("examples/scientific_notation_s3_srvr_14_false-unreach.json",	"NON-TERMINATING", "both",        CPUS),    
    ("examples/polyrank4.t2_2nested.json",				"TERMINATING",     "both",        CPUS),    
    ("examples/let_op_fixpoint_RanFile023.json",			"NON-TERMINATING", "both",        CPUS),    
    ("examples/affine_rf_with_div_aux.json",				"TERMINATING",     "both",        CPUS),    
    ("examples/DivMinus2_no-overflow_term.json",			"TERMINATING",     "both",        CPUS),    
    ("examples/CallNTimes_bpl_gnta.json",				"NON-TERMINATING", "both",        CPUS),    
    ("examples/threadpooling_product_WithProcedures_gnta.json", 	"NON-TERMINATING", "both",        CPUS),
    ("examples/test_array_index_equal_syntactic.json",			"TERMINATING",     "both"),
    ("examples/test_array_index_not_equal_proved.json",		"NON-TERMINATING", "both"),
    ("examples/test_array_index_equal_proved.json",			"TERMINATING",     "both"),
    ("examples/test_array_index_unknown_case_split.json",		"NON-TERMINATING", "both"),
    ("examples/test_array_chain_outer_equal_skip.json",		"TERMINATING",     "both"),
    ("examples/test_array_chain_not_equal_then_equal.json",		"TERMINATING",     "both"),
    ("examples/test_array_chain_double_unknown.json",			"NON-TERMINATING", "both"),
    ("examples/test_array_nested_store_equality_2d.json",		"TERMINATING",     "both"),
    ("examples/test_array_nested_store_equality_or.json",		"TERMINATING",     "both"),
    ("examples/test_array_nested_store_equality_3d.json",		"UNKNOWN",         "both"),
    ("examples/test_array_chain_triple_unknown.json",			"NON-TERMINATING", "both"),
    ("examples/test_array_scoping_unrelated_indices.json",		"TERMINATING",     "both"),
    ("examples/test_array_scoping_two_arrays.json",			"TERMINATING",     "both"),
    ("examples/test_fixpoint_array_state_real_4bitcounter.json",	"UNKNOWN",         "both"),
    ("examples/test_fixpoint_array_state_change.json",			"TERMINATING",     "both"),
    ("examples/test_geometric_array_select_real_commoncell.json",	"TERMINATING",      "both"),
    ("examples/test_array_promotion_noninvariant_index.json",		"UNKNOWN",         "both"),
    ("examples/test_array_promotion_cell_in_ranking.json",		"TERMINATING",     "both"),
    ("examples/multiphase_3Phase_term.json",                            "TERMINATING",     "both"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",           "TERMINATING",     "both",        CPUS),
    ("examples/array/arr_CookSeeZuleger_Fig3_2Dcell_term.json",         "TERMINATING",     "both",        CPUS),
    ("examples/array/arr_a05_alloca_term.json",                         "TERMINATING",     "both",        CPUS),
    ("examples/array/arr_basename3_lex_desync_term.json",               "TERMINATING",     "both",        CPUS),
    ("examples/array/arr_array04_alloca_nonterm.json",                  "NON-TERMINATING", "both",        CPUS),
    ("examples/array/arr_Arrays02_equiv_const_idx_nonterm.json",        "NON-TERMINATING", "both",        CPUS),
    ("examples/array/arr_GasCake02_nonterm.json",                       "NON-TERMINATING", "both",        CPUS),
    ("examples/array/arr_NonTermination3_nonterm.json",                 "NON-TERMINATING", "both",        CPUS),
    ("examples/array/arr_printf_nonterm.json",                          "NON-TERMINATING", "both",        CPUS),
]


# ---------------------------------------------------------------------------
# Cases additionally run WITH the ranking-function validator (-val).
# ---------------------------------------------------------------------------

VAL_CASES = [
    # Classic termination (affine / nested / lex / multiphase / piecewise paths).
    ("examples/test_variable_decrease.json",                            "TERMINATING",     "both", CPUS),
    ("examples/multiplication_termination.json",                        "TERMINATING",     "both", CPUS),
    ("examples/test_ranking_func_with_two_variables.json",              "TERMINATING",     "both", CPUS),
    ("examples/test_nested_template_terminating.json",                  "TERMINATING",     "both", CPUS),
    ("examples/test_simple_counter.json",                               "TERMINATING",     "both", CPUS),
    ("examples/DivMinus2_no-overflow_term.json",                        "TERMINATING",     "both", CPUS),
    # Array lassos (cell-scalar encoding) — TERM validated, NT unaffected by -val.
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",           "TERMINATING",     "both", CPUS),
    ("examples/array/arr_CookSeeZuleger_Fig3_2Dcell_term.json",         "TERMINATING",     "both", CPUS),
    ("examples/array/arr_a05_alloca_term.json",                         "TERMINATING",     "both", CPUS),
    ("examples/array/arr_basename3_lex_desync_term.json",               "TERMINATING",     "both", CPUS),
    ("examples/array/arr_array04_alloca_nonterm.json",                  "NON-TERMINATING", "both", CPUS),
    ("examples/array/arr_Arrays02_equiv_const_idx_nonterm.json",        "NON-TERMINATING", "both", CPUS),
    ("examples/array/arr_GasCake02_nonterm.json",                       "NON-TERMINATING", "both", CPUS),
    ("examples/array/arr_NonTermination3_nonterm.json",                 "NON-TERMINATING", "both", CPUS),
    ("examples/array/arr_printf_nonterm.json",                          "NON-TERMINATING", "both", CPUS),
]


# ---------------------------------------------------------------------------
# Cases run with BOTH -val AND -only <template>, so a single template's own
# certificate (not whichever technique wins the portfolio race) decides the
# verdict.
#
# ---------------------------------------------------------------------------

#
# Expected verdicts below are per-solver, NOT uniformly "TERMINATING": isolated
# from the rest of the portfolio, a template that has no proof shaped for its
# own template (e.g. PiecewiseTemplate alone has no valid piecewise-shaped
# certificate for arr_a05 -- AffineTemplate is what actually solves it in
# portfolio mode) legitimately reports UNKNOWN, and CVC5 sometimes fails to
# find within timeout a proof Z3 finds (or vice-versa). Each value here was
# confirmed empirically post-fix; asserting the exact verdict (rather than
# skipping the UNKNOWN cases) is deliberate -- if the phantom-var exploit
# ever comes back, an isolated template could suddenly flip from a genuine
# UNKNOWN to a bogus TERMINATING, which is exactly what this must catch.
ONLY_VAL_CASES = [
    # (file, expected, mode, template, solver)
    ("examples/array/arr_a05_alloca_term.json",               "TERMINATING", "terminate", "affine",        "z3"),
    ("examples/array/arr_a05_alloca_term.json",                "TERMINATING", "terminate", "affine",        "cvc5"),
    ("examples/array/arr_a05_alloca_term.json",                "TERMINATING", "terminate", "nested",        "z3"),
    ("examples/array/arr_a05_alloca_term.json",                "UNKNOWN",     "terminate", "nested",        "cvc5"),
    ("examples/array/arr_a05_alloca_term.json",                "TERMINATING", "terminate", "lexicographic", "z3"),
    ("examples/array/arr_a05_alloca_term.json",                "TERMINATING", "terminate", "lexicographic", "cvc5"),
    # isolated single-template check.
    ("examples/array/arr_a05_alloca_term.json",                "UNKNOWN",     "terminate", "multiphase",    "z3"),
    ("examples/array/arr_a05_alloca_term.json",                "UNKNOWN",     "terminate", "multiphase",    "cvc5"),
    ("examples/array/arr_a05_alloca_term.json",                "UNKNOWN",     "terminate", "piecewise",     "z3"),
    ("examples/array/arr_a05_alloca_term.json",                "UNKNOWN",     "terminate", "piecewise",     "cvc5"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "affine",        "z3"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "affine",        "cvc5"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "nested",        "z3"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "UNKNOWN",     "terminate", "nested",        "cvc5"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "lexicographic", "z3"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "UNKNOWN",     "terminate", "lexicographic", "cvc5"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "multiphase",    "z3"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "multiphase",    "cvc5"),
    # The concrete instance the phantom-var exploit was found on (see the
    # module-level comment above): PiecewiseTemplate alone, z3, WAS able to
    # find a certificate here -- it just used to be an unsound one.
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "TERMINATING", "terminate", "piecewise",     "z3"),
    ("examples/array/arr_Arrays01_equiv_const_idx_term.json",  "UNKNOWN",     "terminate", "piecewise",     "cvc5"),

    # Isolated-template lock-in for the phi_bound fix itself: MultiphaseTemplate
    # alone, -val-checked, must still prove Ultimate's own canonical 3-phase
    # example (see the CASES entry above for the full portfolio version).
    ("examples/multiphase_3Phase_term.json",                   "TERMINATING", "terminate", "multiphase",    "z3"),
    ("examples/multiphase_3Phase_term.json",                   "TERMINATING", "terminate", "multiphase",    "cvc5"),
]


def make_test(file, expected, mode, cpus=1, solver="z3", validate=False, only=None):
    def test_method(self):
        cmd = [PASTTEL_BIN, file, "-a", mode, "-q", "-c", str(cpus), "-s", solver, "-t", "200"]
        if validate:
            cmd.append("-val")   # re-check the synthesized ranking function
        if only:
            cmd += ["-only", only]
        result = subprocess.run(cmd, capture_output=True, text=True)
        output = result.stdout + result.stderr
        overall = next((l for l in output.splitlines() if "OVERALL RESULT" in l), "")
        self.assertIn(
            expected, overall,
            f"\nFile   : {file}\nMode   : {mode}\nValidate: {validate}"
            f"\nExpected: {expected}\nGot    : {overall or '(no OVERALL RESULT line)'}"
        )
    return test_method


# Dynamically build a TestCase class from CASES
attrs = {}
for solver in ["cvc5","z3"]:#, "cvc5"]:
    for entry in CASES:
        file, expected, mode = entry[0], entry[1], entry[2]
        cpus = entry[3] if len(entry) > 3 else 1
        # Test name: strip path and extension, append mode
        name = "test_" + os.path.splitext(os.path.basename(file))[0] + "__" + mode + "__" + solver
        # Deduplicate names (same file tested with different modes)
        base, i = name, 1
        while name in attrs:
            name = f"{base}_{i}"
            i += 1
        attrs[name] = make_test(file, expected, mode, cpus, solver)

# Same cases, but WITH the -val ranking-function validator enabled.
for solver in ["cvc5", "z3"]:
    for entry in VAL_CASES:
        file, expected, mode = entry[0], entry[1], entry[2]
        cpus = entry[3] if len(entry) > 3 else 1
        name = "test_val_" + os.path.splitext(os.path.basename(file))[0] + "__" + mode + "__" + solver
        base, i = name, 1
        while name in attrs:
            name = f"{base}_{i}"
            i += 1
        attrs[name] = make_test(file, expected, mode, cpus, solver, validate=True)

# Same idea, but pinning down a single template's own certificate via -only.
# Solver is fixed per entry (see ONLY_VAL_CASES comment) -- not looped over
# both, since the expected verdict genuinely differs by solver here.
for file, expected, mode, only, solver in ONLY_VAL_CASES:
    name = "test_only_" + only + "_" + os.path.splitext(os.path.basename(file))[0] + "__" + solver
    base, i = name, 1
    while name in attrs:
        name = f"{base}_{i}"
        i += 1
    attrs[name] = make_test(file, expected, mode, CPUS, solver, validate=True, only=only)

NonRegressionTests = type("NonRegressionTests", (unittest.TestCase,), attrs)


# ---------------------------------------------------------------------------
# ArrayHandler transformation cases — format: (file, expected "[ArrayHandler]
# After:" text)
#
# These check ArrayHandler's own text transformation directly, independent of
# the overall verdict: for NOT_EQUAL/UNKNOWN cases, non-termination detection
# (Fixpoint) can reach the correct answer via Z3's own native array theory
# without ever exercising ArrayHandler (it "wins the race" before
# RankingBased's linearize() call runs). "-a terminate" forces RankingBased
# to run regardless, so ArrayHandler's transformation is always exercised.
#
# Aux var names (arr__ite__N) come from a process-wide atomic counter shared
# across every linearize() call in the run, so their exact number is not
# stable across invocations -- normalize_aux_vars() canonicalizes them by
# order of first appearance before comparing.
# ---------------------------------------------------------------------------

ARRAY_HANDLER_CASES = [
    ("examples/test_array_index_equal_syntactic.json",
     "(and (> v_i_2 0) (= v_x_1 1) (= v_i_3 (- v_i_2 v_x_1)))"),
    ("examples/test_array_index_not_equal_proved.json",
     "(and (> v_n_2 0) (> v_m_2 (+ v_n_2 1)) (= v_x_1 arrcell__A__m__inv) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_index_equal_proved.json",
     "(and (> v_n_2 0) (= v_m_2 v_n_2) (= v_x_1 4) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_index_unknown_case_split.json",
     "(and (and (> v_n_2 0) (= v_x_1 arr__ite__0) (= v_n_3 (- v_n_2 v_x_1))) "
     "(or (or (< v_n_2 v_m_2) (> v_n_2 v_m_2)) (and (<= arr__ite__0 4) (>= arr__ite__0 4))) "
     "(or (and (<= v_n_2 v_m_2) (>= v_n_2 v_m_2)) (and (<= arr__ite__0 arrcell__A__m__inv) (>= arr__ite__0 arrcell__A__m__inv))))"),
    ("examples/test_array_chain_outer_equal_skip.json",
     "(and (> v_n_2 0) (= v_m_2 v_n_2) (= v_x_1 4) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_chain_not_equal_then_equal.json",
     "(and (> v_n_2 0) (> v_m_2 v_n_2) (= v_k_2 v_m_2) (= v_x_1 3) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_chain_double_unknown.json",
     "(and (and (> v_n_2 0) (= v_x_1 arr__ite__1) (= v_n_3 (- v_n_2 v_x_1))) "
     "(or (or (< v_m_2 v_k_2) (> v_m_2 v_k_2)) (and (<= arr__ite__0 1) (>= arr__ite__0 1))) "
     "(or (and (<= v_m_2 v_k_2) (>= v_m_2 v_k_2)) (and (<= arr__ite__0 arrcell__A__k__inv) (>= arr__ite__0 arrcell__A__k__inv))) "
     "(or (or (< v_n_2 v_k_2) (> v_n_2 v_k_2)) (and (<= arr__ite__1 2) (>= arr__ite__1 2))) "
     "(or (and (<= v_n_2 v_k_2) (>= v_n_2 v_k_2)) (and (<= arr__ite__1 arr__ite__0) (>= arr__ite__1 arr__ite__0))))"),
    ("examples/test_array_chain_triple_unknown.json",
     "(and (and (> v_n_2 0) (= v_x_1 arr__ite__2) (= v_n_3 (- v_n_2 v_x_1))) "
     "(or (or (< v_k_2 v_p_2) (> v_k_2 v_p_2)) (and (<= arr__ite__0 1) (>= arr__ite__0 1))) "
     "(or (and (<= v_k_2 v_p_2) (>= v_k_2 v_p_2)) (and (<= arr__ite__0 arrcell__A__p__inv) (>= arr__ite__0 arrcell__A__p__inv))) "
     "(or (or (< v_m_2 v_p_2) (> v_m_2 v_p_2)) (and (<= arr__ite__1 2) (>= arr__ite__1 2))) "
     "(or (and (<= v_m_2 v_p_2) (>= v_m_2 v_p_2)) (and (<= arr__ite__1 arr__ite__0) (>= arr__ite__1 arr__ite__0))) "
     "(or (or (< v_n_2 v_p_2) (> v_n_2 v_p_2)) (and (<= arr__ite__2 3) (>= arr__ite__2 3))) "
     "(or (and (<= v_n_2 v_p_2) (>= v_n_2 v_p_2)) (and (<= arr__ite__2 arr__ite__1) (>= arr__ite__2 arr__ite__1))))"),
    ("examples/test_array_nested_store_equality_2d.json",
     "(and (> v_n_2 0) (> v_j_2 v_i_2) "
     "(and (<= (select (select v_A_3 v_i_2) v_j_2) 4) (>= (select (select v_A_3 v_i_2) v_j_2) 4)) "
     "(= v_x_1 (select (select v_A_3 v_i_2) v_j_2)) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_nested_store_equality_or.json",
     "(and (> v_n_2 0) (> v_j_2 v_i_2) (= v_i_2 1) "
     "(or (and (= v_i_2 1) "
     "(and (<= (select (select v_A_3 v_i_2) v_j_2) 4) (>= (select (select v_A_3 v_i_2) v_j_2) 4))) "
     "(and (= v_i_2 2) (= v_A_3 v_A_2))) "
     "(= v_x_1 (select (select v_A_3 v_i_2) v_j_2)) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_nested_store_equality_3d.json",
     "(and (> v_n_2 0) (> v_j_2 v_i_2) (> v_k_2 v_j_2) "
     "(and (<= (select (select (select v_A_3 v_i_2) v_j_2) v_k_2) 5) (>= (select (select (select v_A_3 v_i_2) v_j_2) v_k_2) 5)) "
     "(= v_x_1 (select (select (select v_A_3 v_i_2) v_j_2) v_k_2)) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_scoping_unrelated_indices.json",
     "(and (> v_n_2 0) (> v_j_2 v_i_2) (> v_m_2 v_k_2) "
     "(and (<= (select (select v_A_3 v_i_2) v_j_2) 4) (>= (select (select v_A_3 v_i_2) v_j_2) 4)) "
     "(= v_x_1 (select (select v_A_3 v_i_2) v_j_2)) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_array_scoping_two_arrays.json",
     "(and (> v_n_2 0) (> v_j_2 v_i_2) "
     "(and (<= arrcell__A__i__out 4) (>= arrcell__A__i__out 4)) "
     "(and (<= arrcell__B__k__out 7) (>= arrcell__B__k__out 7)) "
     "(= v_x_1 arrcell__A__i__out) (= v_y_1 arrcell__B__k__out) (= v_n_3 (- v_n_2 v_x_1)))"),
    ("examples/test_fixpoint_array_state_change.json",
     "(and (< arrcell__A__i__in 5) "
     "(and (<= arrcell__A__i__out (+ arrcell__A__i__in 1)) (>= arrcell__A__i__out (+ arrcell__A__i__in 1))))"),
]


def normalize_aux_vars(text):
    """Canonicalize arr__ite__N names by order of first appearance.

    The underlying counter is a single process-wide atomic shared across
    every linearize() call in a run, so the exact N is not stable across
    invocations -- only the *structure* (which occurrences share a name)
    is meaningful.
    """
    seen = {}
    def repl(m):
        name = m.group(0)
        if name not in seen:
            seen[name] = f"AUX{len(seen)}"
        return seen[name]
    return re.sub(r"arr__ite__\d+", repl, text)


def make_array_handler_test(file, expected_after):
    def test_method(self):
        # ArrayHandler's transformation happens during parsing, seconds before
        # any ranking-function search starts -- capture it via a short timeout
        # rather than waiting for "-a terminate" to exhaustively search every
        # template/config/component (genuinely non-terminating array cases
        # force that whole search to run to completion and fail, which can
        # take much longer than the answer we actually need here).
        try:
            result = subprocess.run(
                [PASTTEL_BIN, file, "-a", "terminate", "-v"],
                capture_output=True, text=True, timeout=10
            )
            output = result.stdout + result.stderr
        except subprocess.TimeoutExpired as e:
            def as_text(x):
                if x is None:
                    return ""
                return x.decode("utf-8", errors="replace") if isinstance(x, bytes) else x
            output = as_text(e.stdout) + as_text(e.stderr)
        match = re.search(r"\[ArrayHandler\] After:\s*(.+)", output)
        self.assertIsNotNone(
            match,
            f"\nFile: {file}\nNo '[ArrayHandler] After:' line found in output.\n"
            f"Output (first 2000 chars):\n{output[:2000]}"
        )
        actual = normalize_aux_vars(match.group(1).strip())
        expected_norm = normalize_aux_vars(expected_after)
        self.assertEqual(
            expected_norm, actual,
            f"\nFile    : {file}\nExpected: {expected_norm}\nGot     : {actual}"
        )
    return test_method


array_handler_attrs = {}
for file, expected_after in ARRAY_HANDLER_CASES:
    name = "test_arrayhandler_" + os.path.splitext(os.path.basename(file))[0]
    array_handler_attrs[name] = make_array_handler_test(file, expected_after)
ArrayHandlerTransformationTests = type(
    "ArrayHandlerTransformationTests", (unittest.TestCase,), array_handler_attrs
)


# ---------------------------------------------------------------------------
# Structural tests for FixpointTechnique's x=x' constraint generation.
# Regression guard for the bug where Array-sorted loop-carried variables were
# excluded from the fixpoint check: a loop that only ever mutates an array
# (no scalar state changes) got 0 constraints added, so the loop formula's
# plain satisfiability was mistaken for a genuine fixpoint -- a false
# NON-TERMINATING verdict whenever the array is the only real loop-carried
# state. Checked via "-a nonterminate" (Fixpoint's own log, independent of
# which non-termination technique wins the portfolio race for the final
# verdict -- see the real concrete case in CASES, which races against
# GeometricTechnique and is not usable to test Fixpoint's fix in isolation).
# ---------------------------------------------------------------------------

FIXPOINT_CONSTRAINT_CASES = [
    ("examples/test_fixpoint_array_state_change.json", "1", ["v_A_1 = v_A_3"]),
]


def make_fixpoint_constraint_test(file, expected_count, expected_assignments):
    def test_method(self):
        try:
            result = subprocess.run(
                [PASTTEL_BIN, file, "-a", "nonterminate", "-v"],
                capture_output=True, text=True, timeout=10
            )
            output = result.stdout + result.stderr
        except subprocess.TimeoutExpired as e:
            def as_text(x):
                if x is None:
                    return ""
                return x.decode("utf-8", errors="replace") if isinstance(x, bytes) else x
            output = as_text(e.stdout) + as_text(e.stderr)
        section_match = re.search(
            r"Ajout des contraintes de point fixe.*?✓ Ajout\xe9 (\d+) contraintes de point fixe",
            output, re.DOTALL
        )
        self.assertIsNotNone(
            section_match,
            f"\nFile: {file}\nNo fixpoint-constraint section found in output.\n"
            f"Output (first 2000 chars):\n{output[:2000]}"
        )
        section = section_match.group(0)
        self.assertEqual(expected_count, section_match.group(1), f"File: {file}")
        assignments = re.findall(r"    • (.+)", section)
        self.assertEqual(expected_assignments, assignments, f"File: {file}")
    return test_method


fixpoint_constraint_attrs = {}
for file, expected_count, expected_assignments in FIXPOINT_CONSTRAINT_CASES:
    name = "test_fixpointconstraints_" + os.path.splitext(os.path.basename(file))[0]
    fixpoint_constraint_attrs[name] = make_fixpoint_constraint_test(file, expected_count, expected_assignments)
FixpointConstraintTests = type(
    "FixpointConstraintTests", (unittest.TestCase,), fixpoint_constraint_attrs
)


# ---------------------------------------------------------------------------
# Structural test for GeometricTechnique's array-select/store guard.
# Regression guard for the bug where an array read/write in the loop
# introduced an aux var (arr__select__N) into the linearized polyhedra that
# was never tied into the eigenvector/honda-state recurrence, letting the
# solver satisfy the per-iteration ray constraints once and report a
# spurious geometric non-termination witness. GeometricTechnique now detects
# any "(select "/"(store " remaining in the raw stem/loop formula AFTER
# ArrayHandler's array-cell promotion pass has run (see
# ArrayHandler::promoteInvariantArrayCells) and skips its search for what
# promotion could not safely resolve -- a non-invariant index in particular
# (test_array_promotion_noninvariant_index.json: the cell moves every
# iteration, so it can't be treated as one persistent state variable).
# test_fixpoint_array_state_change.json used to be the example here (its
# array cell has an invariant index) -- now that promotion handles it, it no
# longer skips at all (moved to CASES, verdict TERMINATING).
# ---------------------------------------------------------------------------

GEOMETRIC_SKIP_CASES = [
    "examples/test_array_promotion_noninvariant_index.json",
]


def make_geometric_skip_test(file):
    def test_method(self):
        try:
            result = subprocess.run(
                [PASTTEL_BIN, file, "-a", "nonterminate", "-v"],
                capture_output=True, text=True, timeout=10
            )
            output = result.stdout + result.stderr
        except subprocess.TimeoutExpired as e:
            def as_text(x):
                if x is None:
                    return ""
                return x.decode("utf-8", errors="replace") if isinstance(x, bytes) else x
            output = as_text(e.stdout) + as_text(e.stderr)
        self.assertIn(
            "an Array-typed variable is mutated by the loop",
            output,
            f"\nFile: {file}\nGeometricTechnique did not report skipping due to array "
            f"select/store.\nOutput (first 2000 chars):\n{output[:2000]}"
        )
    return test_method


geometric_skip_attrs = {}
for file in GEOMETRIC_SKIP_CASES:
    name = "test_geometricskip_" + os.path.splitext(os.path.basename(file))[0]
    geometric_skip_attrs[name] = make_geometric_skip_test(file)
GeometricArraySkipTests = type(
    "GeometricArraySkipTests", (unittest.TestCase,), geometric_skip_attrs
)


# ---------------------------------------------------------------------------
# Structural test: Supporting Invariants now reach the actual returned
# report/certificate (proof.proof_details), not just verbose console output
# (they used to be printed only inside GenericTerminationSynthesizer under
# VERBOSITY==VERBOSE and never copied into ProofCertificate at all -- see
# RankingBasedTechnique::analyze()). Checked WITHOUT -v, since the whole
# point is that this no longer needs verbose mode to be visible.
# ---------------------------------------------------------------------------

SI_CERTIFICATE_CASES = [
    ("examples/test_variable_decrease.json",
     ["Supporting invariants:", "[0] y - 2 >= 0", "[1] 1 >= 0"]),
]


def make_si_certificate_test(file, expected_lines):
    def test_method(self):
        try:
            result = subprocess.run(
                [PASTTEL_BIN, file, "-a", "terminate"],
                capture_output=True, text=True, timeout=10
            )
            output = result.stdout + result.stderr
        except subprocess.TimeoutExpired as e:
            def as_text(x):
                if x is None:
                    return ""
                return x.decode("utf-8", errors="replace") if isinstance(x, bytes) else x
            output = as_text(e.stdout) + as_text(e.stderr)
        for expected in expected_lines:
            self.assertIn(
                expected, output,
                f"\nFile: {file}\nExpected '{expected}' in non-verbose report.\n"
                f"Output (first 2000 chars):\n{output[:2000]}"
            )
    return test_method


si_certificate_attrs = {}
for file, expected_lines in SI_CERTIFICATE_CASES:
    name = "test_sicert_" + os.path.splitext(os.path.basename(file))[0]
    si_certificate_attrs[name] = make_si_certificate_test(file, expected_lines)
SupportingInvariantCertificateTests = type(
    "SupportingInvariantCertificateTests", (unittest.TestCase,), si_certificate_attrs
)


if __name__ == "__main__":
    # Write JUnit XML to test-reports/ if xmlrunner is available, else use default runner
    os.makedirs("test-reports", exist_ok=True)
    try:
        import xmlrunner
        runner = xmlrunner.XMLTestRunner(output="test-reports", verbosity=2)
    except ImportError:
        runner = unittest.TextTestRunner(verbosity=2)

    result = unittest.main(module=__name__, testRunner=runner, exit=False)
    sys.exit(0 if result.result.wasSuccessful() else 1)
