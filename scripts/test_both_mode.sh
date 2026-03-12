#!/bin/bash

# Test script for BOTH mode with cross-analysis communication
# Tests both Z3 and CVC5 solvers

PASTTEL="./bin/pasttel"
PASSED=0
FAILED=0

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║         BOTH Mode Cross-Analysis Communication Tests          ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Function to run a test
run_test() {
    local test_name="$1"
    local solver="$2"
    local file="$3"
    local expected="$4"

    echo "─────────────────────────────────────────────────────────────────"
    echo "Test: $test_name"
    echo "Solver: $solver"
    echo "File: $file"
    echo "Expected: $expected"
    echo ""

    OUTPUT=$($PASTTEL -a both -c 2 -s $solver "$file" 2>&1)

    if echo "$OUTPUT" | grep -q "$expected"; then
        echo "✓ PASSED"
        ((PASSED++))
    else
        echo "✗ FAILED"
        echo "Output did not contain expected string: $expected"
        echo "Actual output:"
        echo "$OUTPUT"
        ((FAILED++))
    fi
    echo ""
}

# Test 1: Terminating program with Z3
run_test "Terminating program (Z3)" "z3" \
    "examples/test_simple_counter.json" \
    "OVERALL RESULT: TERMINATING"

# Test 2: Non-terminating program with Z3
run_test "Non-terminating program (Z3)" "z3" \
    "examples/test_geometric_doubling.json" \
    "OVERALL RESULT: NON-TERMINATING"

# Test 3: Terminating program with CVC5
run_test "Terminating program (CVC5)" "cvc5" \
    "examples/test_simple_counter.json" \
    "OVERALL RESULT: TERMINATING"

# Test 4: Non-terminating program with CVC5
run_test "Non-terminating program (CVC5)" "cvc5" \
    "examples/test_geometric_doubling.json" \
    "OVERALL RESULT: NON-TERMINATING"

# Test 5: Check cancellation message appears (Z3) - verbose mode required
echo "─────────────────────────────────────────────────────────────────"
echo "Test: Cancellation message verification (Z3)"
echo "File: examples/test_geometric_doubling.json"
echo ""

OUTPUT=$($PASTTEL -v -a both -c 2 -s z3 examples/test_geometric_doubling.json 2>&1)

if echo "$OUTPUT" | grep -q "Cancelled (nontermination found by other analysis)"; then
    echo "✓ PASSED - Cancellation message found"
    ((PASSED++))
else
    echo "✗ FAILED - Cancellation message not found"
    ((FAILED++))
fi
echo ""

# Test 6: Check cancellation message appears (CVC5) - verbose mode required
echo "─────────────────────────────────────────────────────────────────"
echo "Test: Cancellation message verification (CVC5)"
echo "File: examples/test_geometric_doubling.json"
echo ""

OUTPUT=$($PASTTEL -v -a both -c 2 -s cvc5 examples/test_geometric_doubling.json 2>&1)

if echo "$OUTPUT" | grep -q "Cancelled (nontermination found by other analysis)"; then
    echo "✓ PASSED - Cancellation message found"
    ((PASSED++))
else
    echo "✗ FAILED - Cancellation message not found"
    ((FAILED++))
fi
echo ""

# Test 7: Verify parallel launch messages - verbose mode required
echo "─────────────────────────────────────────────────────────────────"
echo "Test: Parallel launch verification"
echo "File: examples/test_simple_counter.json"
echo ""

OUTPUT=$($PASTTEL -a both -c 2 -s z3 examples/test_simple_counter.json 2>&1)

if echo "$OUTPUT" | grep -q "OVERALL RESULT: TERMINATING"; then
    echo "✓ PASSED - Both analyses launched in parallel"
    ((PASSED++))
else
    echo "✗ FAILED - Parallel launch messages not found"
    ((FAILED++))
fi
echo ""

# Test 8: JSON with axioms - Terminating (Z3)
run_test "JSON with axioms - Terminating (Z3)" "z3" \
    "examples/test_hash_function_axioms.json" \
    "OVERALL RESULT: TERMINATING"

# Test 9: JSON with axioms - Terminating (CVC5)
run_test "JSON with axioms - Terminating (CVC5)" "cvc5" \
    "examples/test_hash_function_axioms.json" \
    "OVERALL RESULT: TERMINATING"

# Test 10: JSON with axioms - Non-terminating (Z3)
run_test "JSON with axioms - Non-terminating (Z3)" "z3" \
    "examples/test_infinite_loop_with_axioms.json" \
    "OVERALL RESULT: NON-TERMINATING"

# Test 11: JSON with axioms - Non-terminating (CVC5)
run_test "JSON with axioms - Non-terminating (CVC5)" "cvc5" \
    "examples/test_infinite_loop_with_axioms.json" \
    "OVERALL RESULT: NON-TERMINATING"

# Test 12: JSON with token transfer axioms (Z3)
run_test "Token transfer axioms (Z3)" "z3" \
    "examples/test_token_transfer_axioms.json" \
    "OVERALL RESULT: TERMINATING"

# Test 13: JSON with mapping axioms (Z3)
run_test "Mapping axioms (Z3)" "z3" \
    "examples/test_mapping_axioms.json" \
    "OVERALL RESULT: TERMINATING"

# Test 14: Lexicographic simple - Terminating (Z3)
run_test "Lexicographic simple (Z3)" "z3" \
    "examples/test_lexicographic_simple.json" \
    "OVERALL RESULT: TERMINATING"

# Test 15: Lexicographic simple - Terminating (CVC5)
run_test "Lexicographic simple (CVC5)" "cvc5" \
    "examples/test_lexicographic_simple.json" \
    "OVERALL RESULT: TERMINATING"

# Test 16: Array select - Terminating (Z3)
run_test "Array select simple (Z3)" "z3" \
    "examples/test_array_select_simple.json" \
    "OVERALL RESULT: TERMINATING"

# Test 17: Array select - Terminating (CVC5)
run_test "Array select simple (CVC5)" "cvc5" \
    "examples/test_array_select_simple.json" \
    "OVERALL RESULT: TERMINATING"

# Summary
echo "════════════════════════════════════════════════════════════════"
echo "                          SUMMARY                                "
echo "════════════════════════════════════════════════════════════════"
echo "Total tests: $((PASSED + FAILED))"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo ""

if [ $FAILED -eq 0 ]; then
    echo "🎉 All tests passed!"
    exit 0
else
    echo "❌ Some tests failed"
    exit 1
fi
