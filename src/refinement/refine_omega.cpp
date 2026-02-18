#include <iostream>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/emptiness.hh>

#include "refinement/refine_omega.h"

RefineOmega::RefineOmega(std::shared_ptr<SMTSolver> solver)
    : mSolver(solver) {}

RefineOmega::Result RefineOmega::refine(
    const LassoProgram& lasso,
    spot::twa_graph_ptr A_D,
    int num_si_strict,
    int num_si_non_strict)
{
    Result result;
    result.is_spurious = false;

    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║         ALGORITHME REFINE_Ω (COMPLET)               ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝" << std::endl;

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 1 : SYNTHÈSE RF + SI
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[PHASE 1] Synthèse RF + SI..." << std::endl;
    
    AffineTemplate affine(num_si_strict, num_si_non_strict);
    GenericTerminationSynthesizer synthesizer(lasso, &affine, mSolver);
    
    auto synthesis_result = synthesizer.synthesize();
    
    if (!synthesis_result.is_valid) {
        std::cout << "❌ UNSAT : Pas de fonction de ranking" << std::endl;
        std::cout << "→ Contre-exemple RÉEL (lasso non-spurious)" << std::endl;
        result.is_spurious = false;
        result.error_message = "No ranking function exists - real counterexample";
        return result;
    }
    
    std::cout << "✓ SAT : Fonction de ranking trouvée" << std::endl;
    synthesizer.printResults(synthesis_result);

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 2 : VALIDATION + FILTRAGE SI
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[PHASE 2] Validation + filtrage SI..." << std::endl;
    
    RankingAndInvariantValidator validator;
    auto validation = validator.validate(
        synthesizer.getRankingFunction(),
        synthesizer.getSupportingInvariants(),
        lasso,
        mSolver
    );
    
    validator.printValidationResult(validation);
    
    if (!validation.is_valid) {
        std::cout << "❌ Validation échouée : " << validation.error_message << std::endl;
        result.is_spurious = false;
        result.error_message = "Validation failed: " + validation.error_message;
        return result;
    }
    
    std::cout << "✓ SI filtrés : " << validation.valid_sis.size() << " valides" << std::endl;

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 3 : ENCODAGE PRÉDICATS
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[PHASE 3] Encodage des prédicats..." << std::endl;
    
    auto predicates = mPredicateEncoder.encode(
        synthesizer.getRankingFunction(),
        validation.valid_sis,
        lasso
    );
    
    mPredicateEncoder.printPredicates(predicates);

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 4 : CONSTRUCTION A_I
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[PHASE 4] Construction automate A_I..." << std::endl;
    
    auto A_I = mAutomatonBuilder.build(predicates, lasso);
    mAutomatonBuilder.printAutomaton(A_I);

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 5 : INTERSECTION A_D ∩ A_I
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[PHASE 5] Intersection A_D ∩ A_I..." << std::endl;
    
    auto product_automaton = spot::product(A_D, A_I);
    
    std::cout << "✓ Produit calculé" << std::endl;
    std::cout << "  États (A_D): " << A_D->num_states() << std::endl;
    std::cout << "  États (A_I): " << A_I->num_states() << std::endl;
    std::cout << "  États (A_D ∩ A_I): " << product_automaton->num_states() << std::endl;

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE 6 : EMPTINESS CHECK
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[PHASE 6] Emptiness check..." << std::endl;
    
    auto emptiness_checker = spot::couvreur99(product_automaton);
    auto run = emptiness_checker->check();
    
    if (!run) {
        // L(A_D ∩ A_I) = ∅ → lasso SPURIEUX
        std::cout << "  L(A_D ∩ A_I) = ∅" << std::endl;
        std::cout << "→ Lasso SPURIEUX (éliminé par A_I)" << std::endl;
        
        result.is_spurious = true;
        result.refined_automaton = A_I;  // A_D sera raffiné avec A_I
        result.error_message = "Spurious lasso eliminated";
    } else {
        // L(A_D ∩ A_I) ≠ ∅ → contre-exemple RÉEL
        std::cout << "  L(A_D ∩ A_I) ≠ ∅" << std::endl;
        std::cout << "→ Contre-exemple RÉEL" << std::endl;
        
        result.is_spurious = false;
        result.error_message = "Real counterexample found in intersection";
    }

    std::cout << "\n  REFINE_Ω TERMINÉ" << std::endl;
    
    return result;
}