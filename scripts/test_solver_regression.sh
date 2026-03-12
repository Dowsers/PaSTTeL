#!/bin/bash

# ============================================================================
# Script de Tests de Non-Régression : Z3 vs CVC5
# ============================================================================
#
# Ce script teste que les algorithmes de terminaison et non-terminaison
# produisent les mêmes résultats avec Z3 et CVC5.
#
# Usage: ./test_solver_regression.sh
#
# ============================================================================

set -e  # Arrêter en cas d'erreur

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Compteurs
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Fonction pour afficher un titre
print_header() {
    echo ""
    echo -e "${BLUE}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║${NC}  $1"
    echo -e "${BLUE}╚═══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

# Fonction pour afficher un test
print_test() {
    echo -e "${YELLOW}[TEST $TOTAL_TESTS]${NC} $1"
}

# Fonction pour afficher un succès
print_success() {
    echo -e "${GREEN}  ✓ $1${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
}

# Fonction pour afficher un échec
print_failure() {
    echo -e "${RED}  ✗ $1${NC}"
    FAILED_TESTS=$((FAILED_TESTS + 1))
}

# Fonction pour tester un exemple avec les deux solvers
test_example() {
    local example=$1
    local mode=$2
    local expected=$3
    local description=$4

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    print_test "$description"

    # Test avec Z3
    local result_z3=$(./bin/pasttel "$example" -a "$mode" -s z3 2>&1 | grep "OVERALL RESULT:" | grep -oE "(TERMINATING|NON-TERMINATING|UNKNOWN)" || echo "ERROR")

    # Test avec CVC5
    local result_cvc5=$(./bin/pasttel "$example" -a "$mode" -s cvc5 2>&1 | grep "OVERALL RESULT:" | grep -oE "(TERMINATING|NON-TERMINATING|UNKNOWN)" || echo "ERROR")

    # Vérification
    if [ "$result_z3" == "$expected" ] && [ "$result_cvc5" == "$expected" ]; then
        print_success "Z3: $result_z3, CVC5: $result_cvc5 (attendu: $expected)"
    else
        print_failure "Z3: $result_z3, CVC5: $result_cvc5 (attendu: $expected)"
        return 1
    fi
}

# Fonction pour tester le mode parallèle
test_parallel() {
    local example=$1
    local mode=$2
    local expected=$3
    local description=$4

    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    print_test "$description (parallèle)"

    # Test avec Z3 parallèle
    local result_z3=$(./bin/pasttel "$example" -a "$mode" -s z3 -c 2 2>&1 | grep "OVERALL RESULT:" | grep -oE "(TERMINATING|NON-TERMINATING|UNKNOWN)" || echo "ERROR")

    # Test avec CVC5 parallèle
    local result_cvc5=$(./bin/pasttel "$example" -a "$mode" -s cvc5 -c 2 2>&1 | grep "OVERALL RESULT:" | grep -oE "(TERMINATING|NON-TERMINATING|UNKNOWN)" || echo "ERROR")

    # Vérification
    if [ "$result_z3" == "$expected" ] && [ "$result_cvc5" == "$expected" ]; then
        print_success "Z3 (//): $result_z3, CVC5 (//): $result_cvc5 (attendu: $expected)"
    else
        print_failure "Z3 (//): $result_z3, CVC5 (//): $result_cvc5 (attendu: $expected)"
        return 1
    fi
}

# ============================================================================
# DÉBUT DES TESTS
# ============================================================================

print_header "Tests de Non-Régression : Z3" # vs CVC5"

echo "Exécutable: ./bin/pasttel"
echo "Date: $(date)"
echo ""

# Vérifier que l'exécutable existe
if [ ! -f "./bin/pasttel" ]; then
    echo -e "${RED}ERREUR: ./bin/pasttel n'existe pas${NC}"
    echo "Compilez d'abord avec: make bin/pasttel"
    exit 1
fi

# ============================================================================
# TESTS DE TERMINAISON
# ============================================================================

print_header "Tests de Terminaison (Mode Séquentiel)"

test_example "examples/test_simple_counter.json" "terminate" "TERMINATING" \
    "Simple counter (devrait terminer)"
test_example "examples/test_variable_decrease.json" "terminate" "TERMINATING" \
    "Variable decrease (devrait terminer)"
test_example "examples/test_ranking_func_with_two_variables.json" "terminate" "TERMINATING" \
    "Two variable decrease (devrait terminer)"
test_example "examples/multiplication_termination.json" "terminate" "TERMINATING" \
    "Multiplication operation (devrait terminer)"
test_example "examples/test_affine_template.json" "terminate" "TERMINATING" \
    "Test affine template (devrait terminer)"
# Tests JSON avec arrays (select)
test_example "examples/test_array_select_simple.json" "terminate" "TERMINATING" \
    "Array select simple (devrait terminer)"
# Tests Lexicographic template
test_example "examples/test_lexicographic_simple.json" "terminate" "TERMINATING" \
    "Lexicographic simple (devrait terminer)"
    
    
# ============================================================================
# TESTS DE NON-TERMINAISON
# ============================================================================

print_header "Tests de Non-Terminaison (Mode Séquentiel)"

test_example "examples/test_unbounded_counter.json" "nonterminate" "NON-TERMINATING" \
	"Unbounded counter (ne devrait pas terminer)"
#test_example "examples/test_geometric_doubling.json" "nonterminate" "NON-TERMINATING" \
#	"Geometric doubling (ne devrait pas terminer)"
test_example "examples/nonterminate_booleans.json" "nonterminate" "NON-TERMINATING" \
	"Boolean operation (ne devrait pas terminer)"
test_example "examples/fixpoint_nontermination.json" "nonterminate" "NON-TERMINATING" \
	"Fixpoint example (ne devrait pas terminer)"
test_example "examples/test_ranking_func_with_two_variables_non_terminating.json" "nonterminate" "NON-TERMINATING" \
	"Two variable unbound (ne sait pas)"

# ============================================================================
# TESTS MODE BOTH
# ============================================================================

print_header "Tests Mode Both (Terminaison + Non-Terminaison, Mode Séquentiel)"

test_example "examples/test_simple_counter.json" "both" "TERMINATING" \
    "Simple counter (devrait terminer)"
test_example "examples/test_variable_decrease.json" "both" "TERMINATING" \
    "Variable decrease (devrait terminer)"
test_example "examples/test_ranking_func_with_two_variables.json" "both" "TERMINATING" \
    "Two variable decrease (devrait terminer)"
test_example "examples/multiplication_termination.json" "both" "TERMINATING" \
    "Multiplication operation (devrait terminer)"
test_example "examples/test_affine_template.json" "both" "TERMINATING" \
    "Test affine template (devrait terminer)"
    
# TODO : fix issue with array parsing sur cvc5
test_example "examples/test_array_select_simple.json" "both" "TERMINATING" \
    "Array select simple (devrait terminer)"
test_example "examples/test_lexicographic_simple.json" "both" "TERMINATING" \
    "Lexicographic simple (devrait terminer)"
test_example "examples/test_unbounded_counter.json" "both" "NON-TERMINATING" \
	"Unbounded counter (ne devrait pas terminer)"
#test_example "examples/test_geometric_doubling.json" "both" "NON-TERMINATING" \
#	"Geometric doubling (ne devrait pas terminer)"
test_example "examples/nonterminate_booleans.json" "both" "NON-TERMINATING" \
	"Boolean operation (ne devrait pas terminer)"
test_example "examples/fixpoint_nontermination.json" "both" "NON-TERMINATING" \
	"Fixpoint example (ne devrait pas terminer)"
test_example "examples/test_ranking_func_with_two_variables_non_terminating.json" "both" "NON-TERMINATING" \
	"Two variable unbound (ne sait pas)"
	
# ============================================================================
# TESTS MODE PARALLÈLE
# ============================================================================

print_header "Tests Mode Parallèle (2 CPUs/approach)"

test_parallel "examples/test_simple_counter.json" "terminate" "TERMINATING" \
    "Simple counter (devrait terminer)"
test_parallel "examples/test_variable_decrease.json" "terminate" "TERMINATING" \
    "Variable decrease (devrait terminer)"
test_parallel "examples/test_ranking_func_with_two_variables.json" "terminate" "TERMINATING" \
    "Two variable decrease (devrait terminer)"
test_parallel "examples/multiplication_termination.json" "terminate" "TERMINATING" \
    "Multiplication operation (devrait terminer)"
test_example "examples/test_affine_template.json" "terminate" "TERMINATING" \
    "Test affine template (devrait terminer)"
    
test_parallel "examples/test_unbounded_counter.json" "nonterminate" "NON-TERMINATING" \
	"Unbounded counter (ne devrait pas terminer)"
#test_parallel "examples/test_geometric_doubling.json" "nonterminate" "NON-TERMINATING" \
#	"Geometric doubling (ne devrait pas terminer)"
test_parallel "examples/nonterminate_booleans.json" "nonterminate" "NON-TERMINATING" \
	"Boolean operation (ne devrait pas terminer)"
test_parallel "examples/fixpoint_nontermination.json" "nonterminate" "NON-TERMINATING" \
	"Fixpoint example (ne devrait pas terminer)"

test_parallel "examples/test_simple_counter.json" "both" "TERMINATING" \
    "Simple counter (devrait terminer, both mode)"
test_parallel "examples/test_variable_decrease.json" "both" "TERMINATING" \
    "Variable decrease (devrait terminer, both mode)"
test_parallel "examples/test_ranking_func_with_two_variables.json" "both" "TERMINATING" \
    "Two variable decrease (devrait terminer, both mode)"
test_parallel "examples/multiplication_termination.json" "both" "TERMINATING" \
    "Multiplication operation (devrait terminer, both mode)"
test_parallel "examples/test_array_select_simple.json" "both" "TERMINATING" \
    "Array select simple (devrait terminer, both mode)"
test_parallel "examples/test_lexicographic_simple.json" "both" "TERMINATING" \
    "Lexicographic simple (devrait terminer,  both mode)"
test_example "examples/test_affine_template.json" "both" "TERMINATING" \
    "Test affine template (devrait terminer)"
    
test_parallel "examples/test_unbounded_counter.json" "both" "NON-TERMINATING" \
	"Unbounded counter (ne devrait pas terminer, both mode)"
#test_parallel "examples/test_geometric_doubling.json" "both" "NON-TERMINATING" \
#	"Geometric doubling (ne devrait pas terminer, both mode)"
test_parallel "examples/nonterminate_booleans.json" "both" "NON-TERMINATING" \
	"Boolean operation (ne devrait pas terminer, both mode)"
test_parallel "examples/fixpoint_nontermination.json" "both" "NON-TERMINATING" \
	"Fixpoint example (ne devrait pas terminer, both mode)"
    
# ============================================================================
# TESTS JSON AVEC AXIOMES
# ============================================================================

print_header "Tests JSON avec Axiomes (Mode Séquentiel)"

test_example "examples/test_hash_function_axioms.json" "terminate" "TERMINATING" \
    "Hash function with injectivity axiom (devrait terminer)"

test_example "examples/test_array_sum_axioms.json" "terminate" "TERMINATING" \
    "Array sum with update axiom (devrait terminer)"

test_example "examples/test_token_transfer_axioms.json" "terminate" "TERMINATING" \
    "Token transfer with balance axioms (devrait terminer)"

test_example "examples/test_mapping_axioms.json" "terminate" "TERMINATING" \
    "Mapping read/write axioms (devrait terminer)"

test_example "examples/test_infinite_loop_with_axioms.json" "nonterminate" "NON-TERMINATING" \
    "Infinite loop with function axiom (ne devrait pas terminer)"

test_example "examples/test_erc20_simple.json" "terminate" "TERMINATING" \
    "ERC20 simple with functions (devrait terminer)"

# ============================================================================
# TESTS JSON AVEC AXIOMES (MODE PARALLÈLE)
# ============================================================================

print_header "Tests JSON avec Axiomes (Mode Parallèle)"

test_parallel "examples/test_hash_function_axioms.json" "terminate" "TERMINATING" \
    "Hash function terminaison"

test_parallel "examples/test_infinite_loop_with_axioms.json" "nonterminate" "NON-TERMINATING" \
    "Infinite loop non-terminaison"

test_parallel "examples/test_lexicographic_simple.json" "terminate" "TERMINATING" \
    "Lexicographic simple terminaison"

# ============================================================================
# RÉSUMÉ
# ============================================================================

print_header "Résumé des Tests"

echo "Total de tests   : $TOTAL_TESTS"
echo -e "Tests réussis    : ${GREEN}$PASSED_TESTS${NC}"
echo -e "Tests échoués    : ${RED}$FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║  ✓ TOUS LES TESTS SONT PASSÉS !                              ║${NC}"
    echo -e "${GREEN}║                                                               ║${NC}"
    echo -e "${GREEN}║  Z3 et CVC5 produisent les mêmes résultats                   ║${NC}"
    echo -e "${GREEN}╚═══════════════════════════════════════════════════════════════╝${NC}"
    exit 0
else
    echo -e "${RED}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║  ✗ CERTAINS TESTS ONT ÉCHOUÉ                                  ║${NC}"
    echo -e "${RED}║                                                               ║${NC}"
    echo -e "${RED}║  Vérifiez les différences entre Z3 et CVC5                    ║${NC}"
    echo -e "${RED}╚═══════════════════════════════════════════════════════════════╝${NC}"
    exit 1
fi
