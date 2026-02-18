#ifndef SUPPORTING_INVARIANT_GENERATOR_H
#define SUPPORTING_INVARIANT_GENERATOR_H

#include <memory>
#include <vector>
#include <string>

#include "lasso_program.h"
#include "linear_inequality.h"
#include "templates/ranking_template.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Générateur des Supporting Invariants (SI) — séparé des templates RF
 *
 * Responsabilités :
 *   - Déclarer les paramètres SMT des SI
 *   - Générer φ1 (stem initiation)  : stem(x,x') → SI(x') ≥ 0
 *   - Générer φ2 (loop consecution) : SI(x) ∧ loop(x,x') → SI(x') ≥ 0
 *   - Fournir SI(x) ≥ 0 comme prémisses pour φ3/φ4 des templates RF
 *
 * Équivalent de SupportingInvariantGenerator dans Ultimate/LassoRanker.
 */
class SupportingInvariantGenerator {
public:
    SupportingInvariantGenerator(int num_si_strict, int num_si_nonstrict);

    /**
     * @brief Initialise avec le programme lasso (SSA vars)
     */
    void init(const LassoProgram& lasso);

    /**
     * @brief Déclare les paramètres SMT des SI dans le solveur
     */
    void declareParameters(std::shared_ptr<SMTSolver> solver) const;

    // ========================================================================
    // GÉNÉRATION DES CONTEXTES MOTZKIN
    // ========================================================================

    /**
     * @brief Génère φ1 : stem(x,x') → SI(x') ≥ 0 (un contexte par SI par polyèdre)
     */
    std::vector<RankingTemplate::MotzkinContext> generatePhi1() const;

    /**
     * @brief Génère φ2 : SI(x) ∧ loop(x,x') → SI(x') ≥ 0
     */
    std::vector<RankingTemplate::MotzkinContext> generatePhi2() const;

    // ========================================================================
    // PRÉMISSES POUR LES TEMPLATES RF
    // ========================================================================

    /**
     * @brief Construit SI_k(vars) ≥ 0 pour chaque SI
     *
     * Ces LinearInequality sont injectées comme prémisses dans les contextes
     * Motzkin des templates RF (φ3, φ4, etc.).
     *
     * @param vars Variables SSA d'entrée du loop
     */
    std::vector<LinearInequality> buildPreconditions(
        const std::vector<std::string>& vars) const;

    // ========================================================================
    // ACCESSEURS POUR L'EXTRACTION (utilisés par GenericTerminationSynthesizer)
    // ========================================================================

    /** @brief Liste plate de tous les noms de paramètres SMT des SI */
    std::vector<std::string> getSIParams() const;

    /** @brief Si SI_k est strict (>) ou non-strict (≥) */
    std::vector<bool> getSIIsStrict() const;

    int getNumSI() const { return num_si_; }

private:
    int num_strict_;
    int num_nonstrict_;
    int num_si_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::vector<std::string>> si_params_;  // [si_idx][var_idx | const]

    LinearInequality buildSI(
        int si_idx,
        const std::vector<std::string>& vars,
        bool is_strict) const;

    void initializeParameters();
};

#endif // SUPPORTING_INVARIANT_GENERATOR_H
