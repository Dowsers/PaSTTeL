#ifndef PORTFOLIO_ORCHESTRATOR_H
#define PORTFOLIO_ORCHESTRATOR_H

#include <memory>
#include <vector>
#include <string>

#include "analysis_technique_interface.h"
#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Orchestrateur de techniques d'analyse en mode portfolio
 *
 * Lance toutes les techniques en parallèle (jusqu'à max_threads simultanément)
 * via std::async, chacune avec son propre clone du solver.
 * Dès qu'une technique retourne un résultat conclusif, annule les autres
 * via cancel() et retourne immédiatement.
 * Avec max_threads == 1, les futures s'exécutent séquentiellement.
 */
class PortfolioOrchestrator {
public:
    explicit PortfolioOrchestrator(int max_threads);

    void addTechnique(std::unique_ptr<AnalysisTechniqueInterface> technique);

    /**
     * @brief Lance toutes les techniques et retourne le premier résultat conclusif
     * @return Premier résultat conclusif, ou UNKNOWN si aucun
     */
    ProofCertificate solve(const LassoProgram& lasso, std::shared_ptr<SMTSolver> solver);

    const std::vector<ProofCertificate>& getAllResults() const;

private:
    int max_threads_;
    std::vector<std::unique_ptr<AnalysisTechniqueInterface>> techniques_;
    std::vector<ProofCertificate> all_results_;

    std::vector<std::shared_ptr<SMTSolver>> prepareSolvers(
        std::shared_ptr<SMTSolver> solver, size_t count) const;
};

#endif // PORTFOLIO_ORCHESTRATOR_H
