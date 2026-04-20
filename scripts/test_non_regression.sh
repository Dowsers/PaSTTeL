#!/bin/bash
# Non-regression test suite for PaSTTeL.
# To add a test case, append a line to CASES:
#   "path/to/file.json|EXPECTED_RESULT|mode[|cpus]"
# cpus is optional (defaults to 1).

set -uo pipefail

PASTTEL_BIN="${PASTTEL_BIN:-./bin/pasttel}"

tests_passed=0
tests_failed=0

run_test() {
    local file="$1" expected="$2" mode="$3" cpus="${4:-1}"

    echo ""
    echo "Test: $file  (mode: $mode, cpus: $cpus)"
    local output
    output=$("$PASTTEL_BIN" "$file" -a "$mode" -q -c "$cpus" 2>&1)

    if echo "$output" | grep "OVERALL RESULT" | grep -q "$expected"; then
        echo "  ✓ PASS"
        ((tests_passed++))
    else
        echo "  ✗ FAIL — expected: $expected"
        echo "$output" | grep "OVERALL RESULT" || true
        ((tests_failed++))
    fi
}

# ---------------------------------------------------------------------------
# Test cases — format: "file|EXPECTED|mode[|cpus]"
# ---------------------------------------------------------------------------

CASES=(
    "examples/test_simple_counter.json|TERMINATING|terminate"
    "examples/test_variable_decrease.json|TERMINATING|terminate"
    "examples/test_ranking_func_with_two_variables.json|TERMINATING|terminate"
    "examples/multiplication_termination.json|TERMINATING|terminate"
    "examples/test_unbounded_counter.json|NON-TERMINATING|nonterminate"
 #   "examples/test_geometric_doubling.json|NON-TERMINATING|nonterminate"
    "examples/nonterminate_booleans.json|NON-TERMINATING|nonterminate"
    "examples/fixpoint_nontermination.json|NON-TERMINATING|nonterminate"
    "examples/test_ranking_func_with_two_variables_non_terminating.json|NON-TERMINATING|nonterminate"
    "examples/test_with_div_mod.json|TERMINATING|both|2"
    "examples/test_with_div_mod_mult.json|TERMINATING|both|2"
    "examples/test_division_termination.json|TERMINATING|both|2"
    "examples/test_nested_template_terminating.json|TERMINATING|both|2"
    "examples/multiplication_termination.json|TERMINATING|both|2"
    "examples/nonterminate_booleans.json|NON-TERMINATING|both|2"
    "examples/fixpoint_nontermination.json|NON-TERMINATING|both|2"
    "examples/test_hash_function_axioms.json|TERMINATING|terminate"
    "examples/test_array_sum_axioms.json|TERMINATING|terminate"
    "examples/test_token_transfer_axioms.json|TERMINATING|terminate"
    "examples/test_mapping_axioms.json|TERMINATING|terminate"
    "examples/test_infinite_loop_with_axioms.json|NON-TERMINATING|nonterminate"
    "examples/test_erc20_simple.json|TERMINATING|terminate"
    "examples/test_array_select_simple.json|TERMINATING|terminate"
    "examples/test_lexicographic_simple.json|TERMINATING|terminate"
    "examples/test_stem_si_phi1.json|TERMINATING|terminate"
)

# ---------------------------------------------------------------------------
# Run all cases
# ---------------------------------------------------------------------------

echo "========================================="
echo "Tests de non-regression PaSTTeL"
echo "========================================="

for entry in "${CASES[@]}"; do
    IFS='|' read -r file expected mode cpus <<< "$entry"
    run_test "$file" "$expected" "$mode" "${cpus:-1}"
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "========================================="
echo "Results: $tests_passed passed, $tests_failed failed"
echo "========================================="

exit $tests_failed
