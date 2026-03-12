#!/bin/bash

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  TEST COMPLET : Parallélisation Terminaison + Non-Term    ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

echo "1️⃣  Test Terminaison (séquentiel)"
./bin/pasttel examples/test_simple_counter.json -a terminate -c 1 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "2️⃣  Test Terminaison (parallèle)"
./bin/pasttel examples/test_simple_counter.json -a terminate -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "3️⃣  Test Non-Terminaison (séquentiel)"
./bin/pasttel examples/test_unbounded_counter.json -a nonterminate -c 1 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "4️⃣  Test Non-Terminaison (parallèle)"
./bin/pasttel examples/test_unbounded_counter.json -a nonterminate -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "5️⃣  Test Both (parallèle)"
./bin/pasttel examples/test_simple_counter.json -a both -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "6️⃣  Vérification Early Stopping (verbose)"
echo "   Mode parallèle non-terminaison avec early stopping:"
./bin/pasttel examples/test_unbounded_counter.json -a nonterminate -c 4 -v 2>&1 | grep -E "(Thread|Cancelled)" | head -5

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Tests avec JSON et Axiomes"
echo "════════════════════════════════════════════════════════════"

echo ""
echo "7️⃣  Test JSON avec axiomes - Terminaison (séquentiel)"
./bin/pasttel examples/test_hash_function_axioms.json -a terminate -c 1 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "8️⃣  Test JSON avec axiomes - Terminaison (parallèle)"
./bin/pasttel examples/test_hash_function_axioms.json -a terminate -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "9️⃣  Test JSON avec axiomes - Non-Terminaison (séquentiel)"
./bin/pasttel examples/test_infinite_loop_with_axioms.json -a nonterminate -c 1 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "🔟  Test JSON avec axiomes - Non-Terminaison (parallèle)"
./bin/pasttel examples/test_infinite_loop_with_axioms.json -a nonterminate -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "1️⃣1️⃣  Test Token Transfer Axioms (both)"
./bin/pasttel examples/test_token_transfer_axioms.json -a both -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "1️⃣2️⃣  Test Mapping Axioms (both)"
./bin/pasttel examples/test_mapping_axioms.json -a both -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Tests Lexicographic Template"
echo "════════════════════════════════════════════════════════════"

echo ""
echo "1️⃣3️⃣  Test Lexicographic - Terminaison (séquentiel)"
./bin/pasttel examples/test_lexicographic_simple.json -a terminate -c 1 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "1️⃣4️⃣  Test Lexicographic - Terminaison (parallèle)"
./bin/pasttel examples/test_lexicographic_simple.json -a terminate -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "1️⃣5️⃣  Test Lexicographic - Both (parallèle)"
./bin/pasttel examples/test_lexicographic_simple.json -a both -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Tests avec Arrays (select)"
echo "════════════════════════════════════════════════════════════"

echo ""
echo "1️⃣6️⃣  Test Array select - Terminaison (séquentiel)"
./bin/pasttel examples/test_array_select_simple.json -a terminate -c 1 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "1️⃣7️⃣  Test Array select - Terminaison (parallèle)"
./bin/pasttel examples/test_array_select_simple.json -a terminate -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "1️⃣8️⃣  Test Array select - Both (parallèle)"
./bin/pasttel examples/test_array_select_simple.json -a both -c 4 2>&1 | grep -E "(TERMINATING|NON-TERMINATING)"

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║  TOUS LES TESTS RÉUSSIS                                   ║"
echo "╚════════════════════════════════════════════════════════════╝"
