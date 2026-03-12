#ifndef RANKING_BASED_TECHNIQUE_H
#define RANKING_BASED_TECHNIQUE_H

#include <memory>
#include <string>
#include <vector>
#include <atomic>

#include "termination/termination_technique_interface.h"
#include "termination/generic_termination_synthesizer.h"
#include "templates/ranking_template.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "analysis_technique_interface.h"

/**
 * @brief Configuration d'un template de ranking
 */
struct TemplateConfig {
    int num_si_strict;
    int num_si_nonstrict;
    std::string description;
};

/**
 * @brief Technique de terminaison basée sur la synthèse de ranking functions
 *
 * Cette technique essaie un template spécifique (Affine, Nested, etc.)
 * avec différentes configurations de supporting invariants.
 *
 * Architecture:
 * - Boucle sur les configurations pour un template donné
 * - Pour chaque configuration: synthèse + validation
 * - Early stopping: arrêt dès qu'une preuve est trouvée
 *
 * Interruptible: peut être annulée via cancel() pour la parallélisation
 */
class RankingBasedTechnique : public AnalysisTechniqueInterface {
public:
    /**
     * @brief Constructeur
     * @param template_name Nom du template à utiliser (ex: "AffineTemplate", "NestedTemplate")
     * @param configs Configurations (num_si_strict, num_si_nonstrict) à essayer
     * @param num_components_nested Nombre de composantes pour NestedTemplate (ignoré pour AffineTemplate)
     */
    RankingBasedTechnique(
        const std::string& template_name,
        const std::vector<TemplateConfig>& configs,
        int num_components_nested = 2);

    // ========================================================================
    // IMPLÉMENTATION DE L'INTERFACE
    // ========================================================================

    void init(const LassoProgram& lasso) override;

    /**
     * @brief Analyse et retourne un AnalysisResult (interface PortfolioOrchestrator)
     */
    AnalysisResult analyze(std::shared_ptr<SMTSolver> solver) override;
    ProofCertificate getProof() const override { return proof_; }

    std::string getName() const override;
    std::string getDescription() const;
    void printResult(const TerminationResult& result) const;

    bool validateConfiguration() const override;
    void printInfo() const;

    bool canBeCancelled() const override { return true; }
    void cancel() override;

    /**
     * @brief Accès au dernier argument de terminaison (pour validation post-synthèse)
     */
    const GenericTerminationSynthesizer& getLastSynthesizer() const { return *last_synthesizer_; }

private:
    // Configuration
    std::string template_name_;
    std::vector<TemplateConfig> configs_;
    int num_components_nested_;

    // Instance du solveur
    std::shared_ptr<SMTSolver> solver_;

    // État
    const LassoProgram* lasso_;
    std::atomic<bool> cancelled_;

    // Résultat de la dernière synthèse réussie
    GenericTerminationSynthesizer::SynthesisResult last_synthesis_result_;
    std::unique_ptr<GenericTerminationSynthesizer> last_synthesizer_;
    ProofCertificate proof_;

    /**
     * @brief Crée un template de terminaison
     */
    RankingTemplate* createTemplate(
        const std::string& template_name,
        int num_si_strict,
        int num_si_nonstrict) const;

    /**
     * @brief Essaie une combinaison template + configuration
     * @return true si une preuve a été trouvée
     */
    bool tryTemplateConfiguration(
        const std::string& template_name,
        const TemplateConfig& config,
        std::shared_ptr<SMTSolver> solver,
        int verbosity);
};

#endif // RANKING_BASED_TECHNIQUE_H
