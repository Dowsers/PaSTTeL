#!/usr/bin/env python3
"""
Non-regression test suite for PaSTTeL.
Produces JUnit XML output readable by Jenkins.

To add a test case, append a tuple to CASES:
    ("path/to/file.json", "EXPECTED_RESULT", "mode")         # cpus defaults to 1
    ("path/to/file.json", "EXPECTED_RESULT", "mode", cpus)
"""

import os
import subprocess
import sys
import unittest

PASTTEL_BIN = os.environ.get("PASTTEL_BIN", "./bin/pasttel")

# ---------------------------------------------------------------------------
# Test cases — format: (file, expected_result, mode[, cpus])
# ---------------------------------------------------------------------------

CASES = [
    ("examples/test_simple_counter.json",                               "TERMINATING",     "terminate"),
    ("examples/test_variable_decrease.json",                            "TERMINATING",     "terminate"),
    ("examples/test_ranking_func_with_two_variables.json",              "TERMINATING",     "terminate"),
    ("examples/multiplication_termination.json",                        "TERMINATING",     "terminate"),
    ("examples/test_unbounded_counter.json",                            "NON-TERMINATING", "nonterminate"),
  # ("examples/test_geometric_doubling.json",                           "NON-TERMINATING", "nonterminate"),
  # ("examples/nonterminate_booleans.json",                             "NON-TERMINATING", "nonterminate"),
    ("examples/fixpoint_nontermination.json",                           "NON-TERMINATING", "nonterminate"),
    ("examples/test_ranking_func_with_two_variables_non_terminating.json", "NON-TERMINATING", "nonterminate"),
    ("examples/test_with_div_mod.json",                                 "TERMINATING",     "both",        2),
    ("examples/test_with_div_mod_mult.json",                            "TERMINATING",     "both",        2),
    ("examples/test_division_termination.json",                         "TERMINATING",     "both",        2),
    ("examples/test_nested_template_terminating.json",                  "TERMINATING",     "both",        2),
    ("examples/multiplication_termination.json",                        "TERMINATING",     "both",        2),
  # ("examples/nonterminate_booleans.json",                             "NON-TERMINATING", "both",        2),
    ("examples/fixpoint_nontermination.json",                           "NON-TERMINATING", "both",        2),
    ("examples/test_hash_function_axioms.json",                         "TERMINATING",     "terminate"),
    ("examples/test_array_sum_axioms.json",                             "TERMINATING",     "terminate"),
    ("examples/test_token_transfer_axioms.json",                        "TERMINATING",     "terminate"),
    ("examples/test_mapping_axioms.json",                               "TERMINATING",     "terminate"),
    ("examples/test_infinite_loop_with_axioms.json",                    "NON-TERMINATING", "nonterminate"),
    ("examples/test_erc20_simple.json",                                 "TERMINATING",     "terminate"),
    ("examples/test_array_select_simple.json",                          "TERMINATING",     "terminate"),
    ("examples/test_lexicographic_simple.json",                         "TERMINATING",     "terminate"),
    ("examples/test_stem_si_phi1.json",                                 "TERMINATING",     "terminate"),
]


def make_test(file, expected, mode, cpus):
    def test_method(self):
        result = subprocess.run(
            [PASTTEL_BIN, file, "-a", mode, "-q", "-c", str(cpus)],
            capture_output=True, text=True
        )
        output = result.stdout + result.stderr
        overall = next((l for l in output.splitlines() if "OVERALL RESULT" in l), "")
        self.assertIn(
            expected, overall,
            f"\nFile   : {file}\nMode   : {mode}\nExpected: {expected}\nGot    : {overall or '(no OVERALL RESULT line)'}"
        )
    return test_method


# Dynamically build a TestCase class from CASES
attrs = {}
for entry in CASES:
    file, expected, mode = entry[0], entry[1], entry[2]
    cpus = entry[3] if len(entry) > 3 else 1
    # Test name: strip path and extension, append mode
    name = "test_" + os.path.splitext(os.path.basename(file))[0] + "__" + mode
    # Deduplicate names (same file tested with different modes)
    base, i = name, 1
    while name in attrs:
        name = f"{base}_{i}"
        i += 1
    attrs[name] = make_test(file, expected, mode, cpus)

NonRegressionTests = type("NonRegressionTests", (unittest.TestCase,), attrs)


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
