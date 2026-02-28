#ifndef SUPPORTING_INVARIANT_GENERATOR_H
#define SUPPORTING_INVARIANT_GENERATOR_H

#include <memory>
#include <vector>
#include <string>

#include "lasso_program.h"
#include "linear_inequality.h"
#include "templates/ranking_template.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "termination/affine_function_generator.h"

/**
 * @brief Generateur des Supporting Invariants (SI)
 *
 * Responsabilites :
 *   - Declarer les parametres SMT des SI
 *   - Generer phi1 (stem initiation)  : stem(x,x') -> SI(x') >= 0
 *   - Generer phi2 (loop consecution) : SI(x) /\ loop(x,x') -> SI(x') >= 0
 *   - Fournir SI(x) >= 0 comme premisses pour les templates RF
 *   - Exposer buildSI/isStrict pour GenericTerminationSynthesizer::buildConstraints()
 *
 */
class SupportingInvariantGenerator {
public:
    /**
     * @param instance_id  Identifiant unique pour nommer les variables SMT (SUP_INVAR_<instance_id>_k_*)
     *                     Par defaut -1 : utilise l'ancien schema SUP_INVAR_k_*
     */
    SupportingInvariantGenerator(int num_si_strict, int num_si_nonstrict, int instance_id = -1);

    /**
     * @brief Initialise avec le programme lasso (SSA vars)
     */
    void init(const LassoProgram& lasso);

    /**
     * @brief Declare les parametres SMT des SI dans le solveur
     */
    void declareParameters(std::shared_ptr<SMTSolver> solver) const;

    // ========================================================================
    // INTERFACE PUBLIQUE POUR buildConstraints() DU SYNTHESIZER
    // ========================================================================

    /**
     * @brief Construit le terme SI_k(vars) comme LinearInequality
     *
     * Retourne l'expression affine SI_k(vars) avec strict=false et motzkin_coef=ANYTHING.
     * L'appelant est responsable de fixer strict et motzkin_coef selon le contexte.
     *
     * @param si_idx  Index du SI (0-based)
     * @param vars    Variables SSA dans l'ordre
     */
    LinearInequality buildSI(
        int si_idx,
        const std::vector<std::string>& vars) const;

    /**
     * @brief True si le SI d'index si_idx est strict (>), false si non-strict (>=)
     */
    bool isStrict(int si_idx) const { return si_idx < num_strict_; }

    // ========================================================================
    // GENERATION DES CONTEXTES MOTZKIN (legacy -- conserve pour LexicographicTemplate)
    // ========================================================================

    /**
     * @brief Genere phi1 : stem(x,x') -> SI(x') >= 0
     */
    std::vector<RankingTemplate::MotzkinContext> generatePhi1() const;

    /**
     * @brief Genere phi2 : SI(x) /\ loop(x,x') -> SI(x') >= 0
     */
    std::vector<RankingTemplate::MotzkinContext> generatePhi2() const;

    // ========================================================================
    // PREMISSES POUR LES TEMPLATES RF (legacy -- conserve pour LexicographicTemplate)
    // ========================================================================

    /**
     * @brief Construit SI_k(vars) >= 0 pour chaque SI comme premisse RF
     */
    std::vector<LinearInequality> buildPreconditions(
        const std::vector<std::string>& vars) const;

    // ========================================================================
    // ACCESSEURS POUR L'EXTRACTION (utilises par GenericTerminationSynthesizer)
    // ========================================================================

    /** @brief Liste plate de tous les noms de parametres SMT des SI */
    std::vector<std::string> getSIParams() const;

    /** @brief Si SI_k est strict (>) ou non-strict (>=) */
    std::vector<bool> getSIIsStrict() const;

    int getNumSI() const { return num_si_; }

private:
    int num_strict_;
    int num_nonstrict_;
    int num_si_;
    int instance_id_;  // -1 = ancien schema, >= 0 = schema avec instance_id

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::unique_ptr<AffineFunctionGenerator>> generators_;

    void initializeGenerators();
};

#endif // SUPPORTING_INVARIANT_GENERATOR_H
