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
 * Gère un pool de N threads (limité par max_threads_) exécutant des
 * techniques d'analyse de terminaison et/ou non-terminaison.
 *
 * Comportement :
 * - Mode séquentiel (max_threads == 1) : essaie chaque technique dans l'ordre,
 *   reset() du solver entre chaque essai. Arrêt dès qu'un résultat conclusif
 *   (TERMINATING ou NON_TERMINATING) est trouvé.
 * - Mode parallèle (max_threads > 1) : lance jusqu'à max_threads techniques
 *   simultanément via std::async, chacune avec son propre clone du solver.
 *   Dès qu'une technique retourne un résultat conclusif, annule les autres
 *   via cancel() et retourne immédiatement.
 *
 * Remplace TerminationAnalyzer, NonTerminationAnalyzer, et la logique de
 * checkBoth() dans pasttel.cpp.
 */
class PortfolioOrchestrator {
public:
    /**
     * @brief Constructeur
     * @param max_threads Nombre maximum de threads parallèles (1 = séquentiel)
     */
    explicit PortfolioOrchestrator(int max_threads);

    /**
     * @brief Ajoute une technique au portfolio
     */
    void addTechnique(std::unique_ptr<AnalysisTechniqueInterface> technique);

    /**
     * @brief Lance toutes les techniques et retourne le premier résultat conclusif
     *
     * Si aucune technique ne trouve de preuve, retourne AnalysisResult avec
     * kind == UNKNOWN.
     *
     * @param lasso Le programme lasso à analyser
     * @param solver Le solveur SMT de base (cloné pour les threads parallèles)
     * @return Premier résultat conclusif, ou UNKNOWN
     */
    AnalysisResult run(const LassoProgram& lasso, std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Retourne tous les résultats collectés (y compris UNKNOWN)
     */
    const std::vector<AnalysisResult>& getAllResults() const;

private:
    int max_threads_;
    std::vector<std::unique_ptr<AnalysisTechniqueInterface>> techniques_;
    std::vector<AnalysisResult> all_results_;

    /**
     * @brief Mode séquentiel : essaie les techniques une à une
     */
    AnalysisResult runSequential(const LassoProgram& lasso,
                                  std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Mode parallèle : lance jusqu'à max_threads_ techniques simultanément
     */
    AnalysisResult runParallel(const LassoProgram& lasso,
                                std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Prépare les solvers pour les threads parallèles
     *
     * Thread 0 reçoit le solver original, les autres reçoivent des clones.
     * Le clonage copie les déclarations (variables, fonctions, axiomes)
     * mais pas les assertions.
     *
     * @param solver Solver de base
     * @param count Nombre de solvers à préparer
     * @return Vecteur de solvers prêts à l'emploi
     */
    std::vector<std::shared_ptr<SMTSolver>> prepareSolvers(
        std::shared_ptr<SMTSolver> solver, size_t count) const;
};

#endif // PORTFOLIO_ORCHESTRATOR_H
