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
 * @brief Synthesizer generique fonctionnant avec n'importe quel RankingTemplate
 *
 * 1. template_->declareParameters(solver) + sig_.declareParameters(solver)
 * 2. buildConstraints() : construit phi1+phi2+phi3+phi4
 * 3. applyMotzkinTransformations()
 * 4. checkSat()
 * 5. extractResults()
 */
class GenericTerminationSynthesizer {
public:
    /**
     * @brief Resultat de la synthese
     */
    struct SynthesisResult {
        bool is_valid;                              // True si SAT
        std::map<std::string, double> parameters;   // Valeurs des parametres (raw)
        std::string template_name;                  // Nom du template utilise
        std::string description;                    // Description

        SynthesisResult() : is_valid(false) {}
    };

    /**
     * @brief Constructeur
     * @param lasso Programme lasso
     * @param template_ptr Template a utiliser
     * @param solver Solveur SMT
     * @param num_si_strict Nombre de SI stricts (geres par SIG)
     * @param num_si_nonstrict Nombre de SI non-stricts (geres par SIG)
     */
    GenericTerminationSynthesizer(
        const LassoProgram& lasso,
        RankingTemplate* template_ptr,
        std::shared_ptr<SMTSolver> solver,
        int num_si_strict = 0,
        int num_si_nonstrict = 0);

    /**
     * @brief Lance la synthese
     * @return Resultat avec parametres si SAT
     */
    SynthesisResult synthesize();

    /**
     * @brief Nombre total de SIGs locaux crees (pour tests)
     */
    int getNumLocalSIGs() const { return static_cast<int>(local_sigs_.size()); }

    /**
     * @brief Recupere l'argument de terminaison (apres synthesize() == SAT)
     */
    const TerminationArgument& getTerminationArgument() const;

    /**
     * @brief Recupere le template utilise
     */
    const RankingTemplate& getTemplate() const { return *template_; }

    /**
     * @brief Affiche les resultats
     */
    void printResults(const SynthesisResult& result) const;

private:
    // Donnees
    const LassoProgram& lasso_;
    RankingTemplate* template_;
    std::shared_ptr<SMTSolver> solver_;
    int num_si_strict_;
    int num_si_nonstrict_;

    // SIGs locaux : un par (poly_loop × template_part), crees par createLocalSIGs()
    // local_sigs_[p * num_template_parts_ + m] = SIG pour branche p, partie m
    std::vector<std::shared_ptr<SupportingInvariantGenerator>> local_sigs_;
    int num_template_parts_ = 0;  // Nombre de parties du template (dec_list.size() + 1)

    // Etat
    bool synthesized_;
    SynthesisResult last_result_;

    // Resultat structure
    TerminationArgument termination_argument_;

    // ========================================================================
    // PIPELINE DE SYNTHESE (Option C : 3 methodes independantes)
    // ========================================================================

    /**
     * @brief Cree et declare les SIGs locaux (un par poly_loop x template_part).
     * Remplit local_sigs_. Doit etre appele avant buildPhi34Contexts/buildPhi12Contexts.
     * new SIG per (loopConj x templatePart).
     */
    void createLocalSIGs();

    /**
     * @brief Construit les contextes phi3/phi4 (dec + bounded) en utilisant local_sigs_.
     * Chaque contexte inclut les premisses SI du SIG local correspondant.
     */
    std::vector<RankingTemplate::MotzkinContext> buildPhi34Contexts() const;

    /**
     * @brief Construit les contextes phi1/phi2 pour tous les SIGs locaux.
     * phi1 : stem -> SI(stem_out) >= 0
     * phi2 : SI(x) /\ loop -> SI(x') >= 0
     */
    std::vector<RankingTemplate::MotzkinContext> buildPhi12Contexts() const;

    /**
     * @brief Applique Motzkin a tous les contextes
     */
    void applyMotzkinTransformations(
        const std::vector<RankingTemplate::MotzkinContext>& contexts);

    /**
     * @brief Extrait les valeurs des parametres depuis le modele SAT
     */
    std::map<std::string, double> extractParameters(
        const RankingTemplate::TemplateParameters& params);

    /**
     * @brief Extrait la ranking function et les SI depuis le modele SAT
     */
    void extractResults();

    // ========================================================================
    // NORMALISATION GCD
    // ========================================================================

    long long computeGCD(
        const std::vector<double>& coefficients,
        double constant) const;

    long long gcd(long long a, long long b) const;

    void normalizeRankingFunction(RankingFunction& rf, long long gcd_value) const;

    void normalizeSupportingInvariant(SupportingInvariant& si, long long gcd_value) const;
};

#endif // GENERIC_TERMINATION_SYNTHESIZER_H
