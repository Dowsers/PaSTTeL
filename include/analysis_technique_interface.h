#ifndef ANALYSIS_TECHNIQUE_INTERFACE_H
#define ANALYSIS_TECHNIQUE_INTERFACE_H

#include <memory>
#include <string>
#include <map>

#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Résultat unifié pour toutes les techniques d'analyse
 *
 * Discriminé par TerminationStatus : UNKNOWN, TERMINATING, ou NON_TERMINATING.
 * Remplace TerminationResult et NonTerminationResult dans l'orchestrateur.
 * Les résultats originaux sont conservés dans les techniques internes
 * pour rétrocompatibilité.
 */
struct AnalysisResult {
    enum class TerminationStatus {
        UNKNOWN,
        TERMINATING,
        NON_TERMINATING
    };

    TerminationStatus status = TerminationStatus::UNKNOWN;
    std::string technique_name;
    std::string description;
    std::string proof_details;
    double execution_time_ms = 0.0;

    std::map<std::string, int64_t> rf_witness;        // terminaison : coefficients RF
    std::map<std::string, double>  nt_witness_state;  // non-terminaison : état témoin

    AnalysisResult() = default;

    bool isConclusive() const {
        return status != TerminationStatus::UNKNOWN;
    }
};

/**
 * @brief Interface unifiée pour toutes les techniques d'analyse
 *
 * Remplace TerminationTechniqueInterface et NonTerminationTechniqueInterface
 * dans l'orchestrateur PortfolioOrchestrator.
 *
 * Les techniques existantes ne sont pas modifiées : elles sont wrappées
 * via des adaptateurs (RankingBasedTechniqueAdapter, FixpointTechniqueAdapter,
 * GeometricTechniqueAdapter) qui convertissent leurs résultats en AnalysisResult.
 */
class AnalysisTechniqueInterface {
public:
    virtual ~AnalysisTechniqueInterface() = default;

    /**
     * @brief Initialise la technique avec le programme lasso
     * DOIT être appelé avant analyze()
     */
    virtual void init(const LassoProgram& lasso) = 0;

    /**
     * @brief Analyse le programme et retourne un résultat unifié
     * @param solver Le solveur SMT à utiliser
     * @return AnalysisResult avec kind != UNKNOWN si une preuve est trouvée
     */
    virtual AnalysisResult analyze(std::shared_ptr<SMTSolver> solver) = 0;

    /**
     * @brief Retourne le nom de la technique (pour logging)
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Valide la configuration de la technique
     */
    virtual bool validateConfiguration() const { return true; }

    /**
     * @brief Demande l'annulation de la technique (pour parallélisation)
     */
    virtual void cancel() {}

    /**
     * @brief Indique si la technique peut être annulée
     */
    virtual bool canBeCancelled() const { return true; }
};

#endif // ANALYSIS_TECHNIQUE_INTERFACE_H
