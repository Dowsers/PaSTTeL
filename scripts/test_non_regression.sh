#!/bin/bash

echo "========================================="
echo "Tests de non-régression"
echo "========================================="

tests_passed=0
tests_failed=0

# Fonction pour tester un fichier
test_file() {
    local file=$1
    local expected=$2
    local mode=$3
    local cpus=$4
    
    [[ $# < 4 ]] && cpus=1;
    
    echo ""
    echo "Test: $file (mode: $mode)"
    output=$(./bin/pasttel "$file" -t "$mode" -q -c $cpus 2>&1)

    if echo "$output" | grep "OVERALL RESULT" | grep -q "$expected"; then
        echo "✓ PASS"
        ((tests_passed++))
    else
        echo "✗ FAIL - Expected: $expected"
        ((tests_failed++))
    fi
}


echo ""
echo "Testing JSON format..."
echo "========================================="

# Tests de terminaison (JSON)
test_file "examples/test_simple_counter.json" "TERMINATING" "terminate"
test_file "examples/test_variable_decrease.json" "TERMINATING" "terminate"
test_file "examples/test_ranking_func_with_two_variables.json" "TERMINATING" "terminate"
test_file "examples/multiplication_termination.json" "TERMINATING" "terminate"

# Tests de non-terminaison (JSON)
test_file "examples/test_unbounded_counter.json" "NON-TERMINATING" "nonterminate"
test_file "examples/test_geometric_doubling.json" "NON-TERMINATING" "nonterminate"
test_file "examples/nonterminate_booleans.json" "NON-TERMINATING" "nonterminate"
test_file "examples/fixpoint_nontermination.json" "NON-TERMINATING" "nonterminate"

# for now, unknown (JSON)
test_file "examples/test_ranking_func_with_two_variables_non_terminating.json" "UNKNOWN" "terminate"
test_file "examples/test_ranking_func_with_two_variables_non_terminating.json" "UNKNOWN" "nonterminate"



echo ""
echo "Testing Parallel approaches with JSON format..."
echo "========================================="

# Tests sur les deux approches en parallèle
test_file "examples/test_with_div_mod.json" "TERMINATING" "both" 2
test_file "examples/test_with_div_mod_mult.json" "TERMINATING" "both" 2
test_file "examples/test_division_termination.json" "TERMINATING" "both" 2
test_file "examples/test_nested_template_terminating.json" "TERMINATING" "both" 2
test_file "examples/multiplication_termination.json" "TERMINATING" "both" 2
test_file "examples/nonterminate_booleans.json" "NON-TERMINATING" "both" 2
test_file "examples/fixpoint_nontermination.json" "NON-TERMINATING" "both" 2



echo ""
echo "Testing JSON format with axioms..."
echo "========================================="

# Tests de terminaison avec axiomes (JSON)
test_file "examples/test_hash_function_axioms.json" "TERMINATING" "terminate"
test_file "examples/test_array_sum_axioms.json" "TERMINATING" "terminate"
test_file "examples/test_token_transfer_axioms.json" "TERMINATING" "terminate"
test_file "examples/test_mapping_axioms.json" "TERMINATING" "terminate"

# Tests de non-terminaison avec axiomes (JSON)
test_file "examples/test_infinite_loop_with_axioms.json" "NON-TERMINATING" "nonterminate"

# Test ERC20 simple
test_file "examples/test_erc20_simple.json" "TERMINATING" "terminate"

echo ""
echo "Testing JSON format with arrays..."
echo "========================================="

# Tests avec arrays (select)
test_file "examples/test_array_select_simple.json" "TERMINATING" "terminate"

echo ""
echo "Testing Lexicographic template..."
echo "========================================="

# Test lexicographic template
test_file "examples/test_lexicographic_simple.json" "TERMINATING" "terminate"

echo ""
echo "Testing phi1 (stem initiation) with non-empty stem..."
echo "========================================="

# Test phi1 verification: stem sets k=5, loop does n' = n - k
test_file "examples/test_stem_si_phi1.json" "TERMINATING" "terminate"

echo ""
echo "========================================="
echo "Résultats: $tests_passed passed, $tests_failed failed"
echo "========================================="

exit $tests_failed
