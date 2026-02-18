#ifndef RANKING_AND_INVARIANT_VALIDATOR_H
#define RANKING_AND_INVARIANT_VALIDATOR_H

#include <memory>
#include <map>
#include <string>
#include <vector>

#include "termination/generic_termination_synthesizer.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * Validateur post-synthèse pour les arguments de termination
 * 
 * Implémente la validation :
 * 1. Vérifications triviales rapides (isFalse, isTrue)
 * 2. Vérifications SMT lourdes (initiation, consécution, décroissance)
 * 
 * Cette validation est ESSENTIELLE pour rejeter les faux positifs du SMT.
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
        
        // Vérifications triviales (RAPIDES)
        bool is_false_check;      // SI trivialement faux ?
        bool is_true_check;       // SI trivialement vrai ?
        
        // Vérifications SMT (LOURDES)
        bool initiation_check;    // stem → SI(x') ?
        bool compatible_check;
        bool consecution_check;   // SI(x) ∧ loop → SI(x') ?
        
        std::map<std::string, double> counterexample;
    };
    
    /**
     * Résultat de validation globale
     */
    struct ValidationResult {
        bool is_valid;
        std::string error_message;
        
        // Résultats pour la ranking function
        bool rf_non_trivial_check;
        bool rf_bounded_check;
        bool rf_decreasing_check;
        std::map<std::string, double> rf_counterexample;
        
        // Résultats pour les SI
        bool all_si_valid;
        std::vector<SIValidationResult> si_results;
    };
    
    /**
     * Constructeur
     */
    RankingAndInvariantValidator();
    
    /**
     * Valide un argument de termination complet
     *
     * @param ranking_function La fonction de ranking synthétisée
     * @param supporting_invariants Les SI synthétisés
     * @param lasso Le programme lasso
     * @param solver Le solveur SMT
     * @return Résultat de validation détaillé
     */
    ValidationResult validate(
        const GenericTerminationSynthesizer::RankingFunction& ranking_function,
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& supporting_invariants,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver);
    
    /**
     * Affiche les résultats de validation
     */
    void printValidationResult(const ValidationResult& result) const;

    const std::vector<GenericTerminationSynthesizer::SupportingInvariant>
        getValidSupportingInvariants() const { return valid_sis; };

private:

    std::vector<GenericTerminationSynthesizer::SupportingInvariant> valid_sis;

    void registerProgramVariablesToSolver(
        std::shared_ptr<SMTSolver> solver,
        const LassoProgram& lasso);

    // ========================================================================
    // VÉRIFICATIONS TRIVIALES (RAPIDES) - SUPPORTING INVARIANTS
    // ========================================================================
    
    /**
     * Vérifie si le SI est trivialement FAUX
     * 
     * SI de la forme: c >= 0 (non-strict) ou c > 0 (strict)
     * - Non-strict: FAUX si c < 0
     * - Strict:     FAUX si c <= 0
     */
    bool checkSIIsFalse(
        const GenericTerminationSynthesizer::SupportingInvariant& si) const;
    
    /**
     * Vérifie si le SI est trivialement VRAI
     * 
     * SI de la forme: c >= 0 (non-strict) ou c > 0 (strict)
     * - Non-strict: VRAI si c >= 0
     * - Strict:     VRAI si c > 0
     */
    bool checkSIIsTrue(
        const GenericTerminationSynthesizer::SupportingInvariant& si) const;
    
    /**
     * Vérifie si le SI a au moins un coefficient de variable non-nul
     */
    bool checkSINonTriviality(
        const GenericTerminationSynthesizer::SupportingInvariant& si) const;
    
    // ========================================================================
    // VÉRIFICATIONS SMT (LOURDES) - SUPPORTING INVARIANTS
    // ========================================================================
    
    /**
     * Vérifie l'initiation : stem(x, x') → SI(x')
     * 
     * Cherche un contre-exemple où stem(x, x') ∧ ¬SI(x') est SAT
     * Si UNSAT → le stem établit bien le SI
     */
    bool checkSIInitiation(
        const GenericTerminationSynthesizer::SupportingInvariant& si,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver,
        std::map<std::string, double>& counterexample);
    
    /**
     * Vérifie que le SI est compatible avec le loop guard.
     * Un SI incompatible avec le loop guard est "vacuously valid" :
     * la consécution est vraie car SI(x) ∧ loop_guard(x) est toujours UNSAT.
     * 
     * Exemple : SI: x <= -1, loop_guard: x >= 0
     * → SI ∧ loop_guard est UNSAT → consécution vacuously true → REJECT
     * 
     * @return true si SI ∧ loop_guard est SAT (compatible)
     */
    bool checkSICompatibleWithLoop(
        const GenericTerminationSynthesizer::SupportingInvariant& si,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver,
        std::map<std::string, double>& counterexample);

    /**
     * Vérifie la consécution : SI(x) ∧ loop(x, x') → SI(x')
     * 
     * Cherche un contre-exemple où SI(x) ∧ loop(x, x') ∧ ¬SI(x') est SAT
     * Si UNSAT → le SI est inductif
     */
    bool checkSIConsecution(
        const GenericTerminationSynthesizer::SupportingInvariant& si,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver,
        std::map<std::string, double>& counterexample);
    
    /**
     * Valide un seul SI (avec toutes les vérifications)
     */
    SIValidationResult validateSingleSI(
        int si_index,
        const GenericTerminationSynthesizer::SupportingInvariant& si,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver);
    
    // ========================================================================
    // VÉRIFICATIONS - RANKING FUNCTION
    // ========================================================================
    
    /**
     * Vérifie que la RF a au moins un coefficient non-nul
     */
    bool checkRFNonTriviality(
        const GenericTerminationSynthesizer::RankingFunction& rf) const;
    
    /**
     * Vérifie que f(x) >= 0 dans la garde du loop
     */
    bool checkRFBounded(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver,
        std::map<std::string, double>& counterexample);
    
    /**
     * Vérifie que f(x) - f(x') >= δ dans le loop
     */
    bool checkRFDecreasing(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& supporting_invariants,
        const LassoProgram& lasso,
        std::shared_ptr<SMTSolver> solver,
        double delta,
        std::map<std::string, double>& counterexample);
};

#endif // RANKING_AND_INVARIANT_VALIDATOR_H
