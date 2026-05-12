#ifndef PORTFOLIO_ORCHESTRATOR_H
#define PORTFOLIO_ORCHESTRATOR_H

#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

#include "analysis_technique_interface.h"
#include "lasso_program.h"
#include "thread_pool.h"

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

    std::map<std::string, Rational> getRankFunctionDetails() const {
        return winner.rf_witness;
    }
    std::string printNTArgument() const {
        return winner.proof_details;
    }
};

/**
 * @brief Orchestrateur de techniques d'analyse en mode portfolio (pool de thread)
 *
 * Utilisation en deux temps :
 *   1. solve()  — lance toutes les techniques en parallèle (non-bloquant)
 *   2. join(t)  — attend jusqu'à t secondes ; retourne le premier résultat
 *                 conclusif trouvé, ou UNKNOWN si timeout ou aucun résultat.
 *
 * Avec max_threads == 1, les techniques s'exécutent séquentiellement.
 */
class PortfolioOrchestrator {
public:
    explicit PortfolioOrchestrator(int max_threads);

    void addTechnique(std::unique_ptr<AnalysisTechniqueInterface> technique);

    /**
     * @brief Enqueue all techniques in a fresh ThreadPool. Returns immediately.
     */
    void solve(LassoProgram& lasso);

    /**
     * @brief Block until all techniques finish or the time limit is reached.
     * @param timelimit_seconds Maximum wait time in seconds (0 = no limit)
     * @return AnalysisReport with all results and the overall verdict
     */
    AnalysisReport join(int timelimit_seconds = 0);

    int getTechniqueCount() const {
        return techniques_.size();
    }

    void setNumberThreads(int n) { max_threads_ = n; }

private:

    // Executed by each worker thread for technique i.
    void runTechnique(size_t i, const LassoProgram& lasso);
    // Cancel all techniques except for ID winner
    void cancelTechniques(size_t winner, bool verbose);
    // Thread-safe verbose logging.
    void log(bool verbose, const std::string& msg) const;

    // ── Configuration ──────────────────────────────────────────
    int max_threads_;
    std::vector<std::unique_ptr<AnalysisTechniqueInterface>> techniques_;

    // ── Per-solve state ────────────────────────────────────────
    std::unique_ptr<ThreadPool>  pool_;

    mutable std::mutex           mutex_;        // guards all_results_, final_result_, and log output
    std::vector<ProofCertificate> all_results_;
    ProofCertificate              final_result_;

    std::atomic<bool>             conclusive_found_{false};
    std::atomic<bool> stop_early_{false};          // conclusive_found_ OU timeout

};

#endif // PORTFOLIO_ORCHESTRATOR_H
