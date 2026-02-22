#ifndef GENERIC_TERMINATION_SYNTHESIZER_H
#define GENERIC_TERMINATION_SYNTHESIZER_H

#include <memory>
#include <vector>
#include <map>
#include <string>

#include "templates/ranking_template.h"
#include "termination/motzkin_transform.h"
#include "termination/termination_argument.h"
#include "termination/supporting_invariant_generator.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Synthesizer générique fonctionnant avec n'importe quel RankingTemplate
 *
 * Workflow:
 * 1. SIG génère φ1/φ2 (SI constraints) + fournit SI(x) ≥ 0 comme prémisses
 * 2. Template génère les contraintes RF (MotzkinContext) avec SI injectés
 * 3. Synthesizer applique Motzkin et encode en SMT
 * 4. Résolution SMT
 * 5. Extraction des résultats (RankingFunction + SupportingInvariants)
 */
class GenericTerminationSynthesizer {
public:
    /**
     * @brief Résultat de la synthèse
     */
    struct SynthesisResult {
        bool is_valid;                              // True si SAT
        std::map<std::string, double> parameters;   // Valeurs des paramètres (raw)
        std::string template_name;                  // Nom du template utilisé
        std::string description;                    // Description

        SynthesisResult() : is_valid(false) {}
    };

    /**
     * @brief Constructeur
     * @param lasso Programme lasso
     * @param template_ptr Template à utiliser
     * @param solver Solveur SMT
     * @param num_si_strict Nombre de SI stricts (gérés par SIG)
     * @param num_si_nonstrict Nombre de SI non-stricts (gérés par SIG)
     */
    GenericTerminationSynthesizer(
        const LassoProgram& lasso,
        RankingTemplate* template_ptr,
        std::shared_ptr<SMTSolver> solver,
        int num_si_strict = 0,
        int num_si_nonstrict = 0);

    /**
     * @brief Lance la synthèse
     * @return Résultat avec paramètres si SAT
     */
    SynthesisResult synthesize();

    /**
     * @brief Récupère l'argument de terminaison (après synthesize() == SAT)
     *
     * Regroupe la ranking function et les supporting invariants.
     */
    const TerminationArgument& getTerminationArgument() const;

    /**
     * @brief Récupère le template utilisé
     */
    const RankingTemplate& getTemplate() const { return *template_; }

    /**
     * @brief Affiche les résultats
     */
    void printResults(const SynthesisResult& result) const;

private:
    // Données
    const LassoProgram& lasso_;
    RankingTemplate* template_;
    std::shared_ptr<SMTSolver> solver_;
    SupportingInvariantGenerator sig_;

    // État
    bool synthesized_;
    SynthesisResult last_result_;

    // Résultat structuré
    TerminationArgument termination_argument_;

    // ========================================================================
    // PIPELINE DE SYNTHÈSE
    // ========================================================================

    /**
     * @brief Déclare les paramètres SMT du template (RF + delta)
     */
    void declareParameters(const RankingTemplate::TemplateParameters& params);

    /**
     * @brief Applique Motzkin à tous les contextes
     */
    void applyMotzkinTransformations(
        const std::vector<RankingTemplate::MotzkinContext>& contexts);

    /**
     * @brief Ajoute les contraintes de non-trivialité
     */
    void addNonTrivialityConstraints(
        const RankingTemplate::TemplateParameters& params);

    /**
     * @brief Extrait les valeurs des paramètres depuis le modèle SAT
     */
    std::map<std::string, double> extractParameters(
        const RankingTemplate::TemplateParameters& params);

    /**
     * @brief Extrait la ranking function et les SI depuis le modèle SAT
     * La RF est déléguée à template_->extractRankingFunctions(), les SI au SIG.
     */
    void extractResults();

    // ========================================================================
    // NORMALISATION GCD
    // ========================================================================

    /**
     * @brief Calcule le GCD de tous les coefficients (incluant constante)
     */
    long long computeGCD(
        const std::vector<double>& coefficients,
        double constant) const;

    /**
     * @brief Algorithme d'Euclide pour calculer GCD(a, b)
     */
    long long gcd(long long a, long long b) const;

    /**
     * @brief Normalise une RankingFunction par son GCD
     */
    void normalizeRankingFunction(RankingFunction& rf, long long gcd_value) const;

    /**
     * @brief Normalise un SupportingInvariant par son GCD
     */
    void normalizeSupportingInvariant(SupportingInvariant& si, long long gcd_value) const;
};

#endif // GENERIC_TERMINATION_SYNTHESIZER_H
