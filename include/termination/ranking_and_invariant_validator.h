#ifndef RANKING_AND_INVARIANT_VALIDATOR_H
#define RANKING_AND_INVARIANT_VALIDATOR_H

#include <memory>
#include <string>
#include <vector>

#include "termination/termination_argument.h"
#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * Validateur post-synthèse des arguments de terminaison (option -val).
 *
 * Revérifie chaque obligation du template contre le lasso : une obligation
 * vaut ssi (loop ∧ SI(x) ∧ ¬obligation) est prouvé UNSAT, loop et stem étant
 * la disjonction de leurs polyèdres. Voir le bloc PRIMITIVES du .cpp.
 */
class RankingAndInvariantValidator {
public:
    /**
     * Résultat de validation pour un seul SI
     */
    struct SIValidationResult {
        int si_index;
        bool is_valid;
        std::string error_message;

        bool initiation_check;    // stem → SI(x') ? (sans stem : SI partout)
        bool consecution_check;   // SI(x) ∧ loop → SI(x') ?
    };
    
    /**
     * Résultat de validation globale
     */
    struct ValidationResult {
        bool is_valid;
        std::string error_message;

        // Résultats pour la ranking function
        bool rf_bounded_check;
        bool rf_decreasing_check;

        // Résultats pour les SI (un SI non prouvé est écarté, pas bloquant)
        bool all_si_valid;
        std::vector<SIValidationResult> si_results;
    };
    
    /**
     * Constructeur
     */
    RankingAndInvariantValidator();
    
    /**
     * Résultat de validation pour un NestedTemplate (k composants)
     */
    struct NestedValidationResult {
        bool is_valid;
        std::string error_message;

        // SI results (partagés entre composants)
        bool all_si_valid;
        std::vector<SIValidationResult> si_results;

        // Par composant : décroissance nested (la borne ne porte que sur le dernier)
        struct ComponentResult {
            int index;
            bool nested_decrease_check;  // fi(x)-fi(x')+f_{i-1}(x)>=0, ou f0-f0'>=delta
        };
        std::vector<ComponentResult> component_results;
        bool last_component_bounded_check;  // f_{k-1}(x) >= 0
    };

    /**
     * Valide un argument produit par AffineTemplate (exactement 1 composante) :
     *   δ > 0,  loop ∧ SI ⇒ f(x) >= 0,  loop ∧ SI ⇒ f(x) - f(x') >= δ
     */
    ValidationResult validate(
        const TerminationArgument& argument,
        const LassoProgram& lasso,
        SMTSolverInterface* solver);

    /**
     * Valide un argument de termination produit par NestedTemplate
     *
     * Vérifie la sémantique nested :
     *   - δ > 0 et f0(x) - f0(x') >= δ
     *   - fi(x) - fi(x') + f_{i-1}(x) >= 0  pour i > 0
     *   - f_{k-1}(x) >= 0  (borne)
     */
    NestedValidationResult validateNested(
        const TerminationArgument& argument,
        const LassoProgram& lasso,
        SMTSolverInterface* solver);

    /**
     * Valide un argument produit par LexicographicTemplate (k composantes).
     * Sémantique (cf. LexicographicTemplate), avec δi > 0 :
     *   - bound_i      : loop ∧ SI ⇒ fi(x) > 0            (toutes les composantes)
     *   - consec_i<k-1 : loop ∧ SI ⇒ fi(x') ≤ fi(x) ∨ ∃ j<i : fj(x)-fj(x') > dj
     *   - decrement    : loop ∧ SI ⇒ ∃ i : fi(x)-fi(x') > di
     */
    ValidationResult validateLexicographic(
        const TerminationArgument& argument,
        const LassoProgram& lasso,
        SMTSolverInterface* solver);

    /**
     * Valide un argument produit par MultiphaseTemplate (k phases).
     * Sémantique (cf. MultiphaseTemplate), avec δi > 0 :
     *   - decr_0      : loop ∧ SI ⇒ f0(x)-f0(x') > δ0
     *   - decr_i≥1    : loop ∧ SI ⇒ fi(x)-fi(x') > δi ∨ f_{i-1}(x) > 0
     *   - bound       : loop ∧ SI ⇒ ∨_i fi(x) > 0
     */
    ValidationResult validateMultiphase(
        const TerminationArgument& argument,
        const LassoProgram& lasso,
        SMTSolverInterface* solver);

    /**
     * Valide un argument produit par PiecewiseTemplate (morceau i actif en x
     * ssi h_i(x) >= 0 ; une garde par morceau, sinon rejet), avec δi > 0 :
     *   - bound_i      : loop ∧ SI ∧ h_i(x) >= 0 ⇒ f_i(x) >= 0
     *   - decrease_i,j : loop ∧ SI ∧ h_i(x) >= 0 ∧ h_j(x') >= 0 ⇒ f_i(x) - f_j(x') >= δi
     *                    (toute paire de morceaux, comme Ultimate)
     *   - exhaustive   : loop ∧ SI ⇒ ∨_i h_i(x) >= 0
     */
    ValidationResult validatePiecewise(
        const TerminationArgument& argument,
        const LassoProgram& lasso,
        SMTSolverInterface* solver);

    /**
     * Affiche les résultats de validation
     */
    void printValidationResult(const ValidationResult& result) const;

    /**
     * Affiche les résultats de validation nested
     */
    void printNestedValidationResult(const NestedValidationResult& result) const;

    const std::vector<SupportingInvariant>
        getValidSupportingInvariants() const { return valid_sis; };

private:

    std::vector<SupportingInvariant> valid_sis;

    void registerProgramVariablesToSolver(
        SMTSolverInterface* solver,
        const LassoProgram& lasso);

    // ========================================================================
    // SUPPORTING INVARIANTS
    // ========================================================================

    /**
     * Initiation : stem(x, x') ∧ ¬SI(x') UNSAT ; sans stem, ¬SI(x) UNSAT
     * (l'état honda est arbitraire). `why` explique un échec.
     */
    bool checkSIInitiation(
        const SupportingInvariant& si,
        const LassoProgram& lasso,
        SMTSolverInterface* solver,
        std::string& why);

    /**
     * Consécution : SI(x) ∧ loop(x, x') ∧ ¬SI(x') UNSAT. `why` explique un échec.
     */
    bool checkSIConsecution(
        const SupportingInvariant& si,
        const LassoProgram& lasso,
        SMTSolverInterface* solver,
        std::string& why);
    
    /**
     * Valide un seul SI (avec toutes les vérifications)
     */
    SIValidationResult validateSingleSI(
        int si_index,
        const SupportingInvariant& si,
        const LassoProgram& lasso,
        SMTSolverInterface* solver);

    /**
     * Valide tous les SI d'un argument. Remplit `si_results_out`, peuple
     * `valid_sis` (SI prouvés : initiation et consécution), et renvoie true
     * ssi tous les SI sont prouvés.
     */
    bool validateAllSupportingInvariants(
        const std::vector<SupportingInvariant>& sis,
        const LassoProgram& lasso,
        SMTSolverInterface* solver,
        std::vector<SIValidationResult>& si_results_out);
    
    // ========================================================================
    // RANKING FUNCTIONS
    // ========================================================================

    // f_i(x) sur les in-vars du loop, f_i(x') sur ses out-vars, en expressions
    // linéaires exactes (défini dans le .cpp).
    struct LoopTerms;

    static bool buildLoopTerms(
        const LassoProgram& lasso,
        const std::vector<RankingFunction>& fs,
        LoopTerms& terms,
        std::string& why);

    // [loop comme disjonction de ses polyèdres, SI valides évalués en x]
    std::vector<std::string> loopContext(const LassoProgram& lasso) const;

    // true ssi loopContext ∧ counterexample est prouvé UNSAT.
    bool holdsOnLoop(
        const LassoProgram& lasso,
        const std::string& counterexample,
        SMTSolverInterface* solver) const;

    // Préambule commun à tous les templates (forme, δ > 0, SI, termes) ;
    // false = argument déjà rejeté.
    bool prepare(
        const TerminationArgument& argument,
        const LassoProgram& lasso,
        SMTSolverInterface* solver,
        const char* template_name,
        ValidationResult& res,
        LoopTerms& terms);
};

#endif // RANKING_AND_INVARIANT_VALIDATOR_H
