#ifndef PORTFOLIO_ORCHESTRATOR_H
#define PORTFOLIO_ORCHESTRATOR_H

#include <future>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "analysis_technique_interface.h"
#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Summary of a complete portfolio analysis run
 */
struct AnalysisReport {
    std::vector<ProofCertificate> termination_results;
    std::vector<ProofCertificate> nontermination_results;
    ProofCertificate winner;
    std::string overall_result = "UNKNOWN";
    double total_time_ms = 0.0;
    double terminating_time_ms = 0.0;
    double nonterminating_time_ms = 0.0;

    bool isTerminating() const {
        return winner.status == AnalysisResult::TERMINATING;
    }

    bool isNonTerminating() const {
        return winner.status == AnalysisResult::NON_TERMINATING;
    }

    std::map<std::string, int64_t> getRankFunctionDetails() const {
        return winner.rf_witness;
    }
    std::string printNTArgument() const {
        return winner.proof_details;
    }
};

/**
 * @brief Orchestrateur de techniques d'analyse en mode portfolio
 *
 * Utilisation en deux temps :
 *   1. solve()  — lance toutes les techniques en parallèle (non-bloquant)
 *   2. join(t)  — attend jusqu'à t secondes ; retourne le premier résultat
 *                 conclusif trouvé, ou UNKNOWN si timeout ou aucun résultat.
 *
 * Avec max_threads == 1, les futures s'exécutent séquentiellement.
 */
class PortfolioOrchestrator {
public:
    explicit PortfolioOrchestrator(int max_threads);

    void addTechnique(std::unique_ptr<AnalysisTechniqueInterface> technique);

    /**
     * @brief Lance toutes les techniques de façon asynchrone (non-bloquant)
     */
    void solve(const LassoProgram& lasso, std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Waits for the analysis to complete or the time limit to expire.
     * @param timelimit_seconds Maximum wait time in seconds (0 = no limit)
     * @return AnalysisReport with all results and the overall verdict
     */
    AnalysisReport join(int timelimit_seconds = 0);

    int getTechniqueCount() const {
        return techniques_.size();
    }

private:
    int max_threads_;
    std::vector<std::unique_ptr<AnalysisTechniqueInterface>> techniques_;
    std::vector<ProofCertificate> all_results_;

    // State shared between solve() and join()
    std::vector<std::future<ProofCertificate>> futures_;
    std::vector<std::shared_ptr<SMTSolver>> thread_solvers_;
    std::atomic<bool> conclusive_found_{false};
    std::mutex result_mutex_;
    ProofCertificate final_result_;

    // Semaphore to limit concurrent threads to max_threads_
    int sem_count_;
    std::mutex sem_mutex_;
    std::condition_variable sem_cv_;

    std::vector<std::shared_ptr<SMTSolver>> prepareSolvers(
        std::shared_ptr<SMTSolver> solver, size_t count) const;
};

#endif // PORTFOLIO_ORCHESTRATOR_H
