#include <iostream>
#include <future>
#include <atomic>
#include <mutex>
#include <chrono>

#include "portfolio_orchestrator.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

PortfolioOrchestrator::PortfolioOrchestrator(int max_threads)
    : max_threads_(max_threads) {}

// ============================================================================
// AJOUT DE TECHNIQUES
// ============================================================================

void PortfolioOrchestrator::addTechnique(
    std::unique_ptr<AnalysisTechniqueInterface> technique) {
    techniques_.push_back(std::move(technique));
}

const std::vector<AnalysisResult>& PortfolioOrchestrator::getAllResults() const {
    return all_results_;
}

// ============================================================================
// POINT D'ENTRÉE PRINCIPAL
// ============================================================================

AnalysisResult PortfolioOrchestrator::run(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    all_results_.clear();

    // Déclarer le contexte du lasso dans le solver de base
    lasso.declareSolverContext(solver);

    if (max_threads_ <= 1) {
        return runSequential(lasso, solver);
    } else {
        return runParallel(lasso, solver);
    }
}

// ============================================================================
// MODE SÉQUENTIEL
// ============================================================================

AnalysisResult PortfolioOrchestrator::runSequential(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose) {
        std::cout << "\n=== Sequential Portfolio Analysis ===\n";
        std::cout << "Techniques to try: " << techniques_.size() << "\n";
    }

    for (size_t i = 0; i < techniques_.size(); ++i) {
        auto& technique = techniques_[i];

        if (verbose) {
            std::cout << "\n[Phase " << (i + 1) << "/" << techniques_.size()
                      << "] Trying: " << technique->getName() << "\n";
        }

        technique->init(lasso);

        if (!technique->validateConfiguration()) {
            if (verbose)
                std::cout << "  Configuration invalid, skipping\n";
            continue;
        }

        solver->reset();

        auto start = std::chrono::high_resolution_clock::now();
        auto result = technique->analyze(solver);
        auto end = std::chrono::high_resolution_clock::now();

        result.execution_time_ms = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        all_results_.push_back(result);

        if (result.isConclusive()) {
            if (verbose) {
                std::cout << "  Result found: "
                          << (result.status == AnalysisResult::TerminationStatus::TERMINATING
                              ? "TERMINATING" : "NON-TERMINATING")
                          << " (in " << result.execution_time_ms << "ms)\n";
            }
            return result;
        }

        if (verbose)
            std::cout << "  No conclusive result (took " << result.execution_time_ms << "ms)\n";
    }

    if (verbose)
        std::cout << "\n  All techniques exhausted — result: UNKNOWN\n";

    AnalysisResult unknown;
    unknown.technique_name = "None";
    unknown.description = "No proof found by any technique";
    return unknown;
}

// ============================================================================
// PRÉPARATION DES SOLVERS PARALLÈLES
// ============================================================================

std::vector<std::shared_ptr<SMTSolver>> PortfolioOrchestrator::prepareSolvers(
    std::shared_ptr<SMTSolver> solver, size_t count) const {

    std::vector<std::shared_ptr<SMTSolver>> solvers;
    solvers.reserve(count);
    solvers.push_back(solver);  // Thread 0 : solver original
    for (size_t i = 1; i < count; ++i) {
        solvers.push_back(solver->clone());
    }
    return solvers;
}

// ============================================================================
// MODE PARALLÈLE
// ============================================================================

AnalysisResult PortfolioOrchestrator::runParallel(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    const size_t n = techniques_.size();
    // Nombre de threads effectifs : min(max_threads_, n)
    const size_t n_threads = std::min(static_cast<size_t>(max_threads_), n);

    if (verbose) {
        std::cout << "\n=== Parallel Portfolio Analysis ===\n";
        std::cout << "Techniques: " << n << ", threads: " << n_threads << "\n";
        for (size_t i = 0; i < n; ++i)
            std::cout << "  * " << techniques_[i]->getName() << "\n";
        std::cout << "\n";
    }

    // Préparer les solvers (un par technique — pas de partage entre threads)
    auto thread_solvers = prepareSolvers(solver, n);

    // Drapeaux partagés
    std::atomic<bool> conclusive_found{false};
    std::mutex result_mutex;
    AnalysisResult final_result;

    std::vector<std::future<AnalysisResult>> futures;
    futures.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        size_t solver_idx = i;

        futures.push_back(std::async(std::launch::async,
            [this, &lasso, &conclusive_found, &result_mutex, &final_result,
             i, solver_idx, &thread_solvers, verbose]() -> AnalysisResult {

                auto& technique = techniques_[i];
                std::string name = technique->getName();

                // Vérifier si déjà annulé
                if (conclusive_found.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[" << name << "] Skipped (result already found)\n";
                    }
                    AnalysisResult r;
                    r.technique_name = name;
                    return r;
                }

                technique->init(lasso);

                if (!technique->validateConfiguration()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[" << name << "] Invalid configuration, skipping\n";
                    }
                    AnalysisResult r;
                    r.technique_name = name;
                    return r;
                }

                auto thread_solver = thread_solvers[solver_idx];
                thread_solver->reset();

                if (verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[" << name << "] Starting...\n";
                }

                auto start = std::chrono::high_resolution_clock::now();
                auto result = technique->analyze(thread_solver);
                auto end = std::chrono::high_resolution_clock::now();

                result.execution_time_ms = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start).count());

                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    all_results_.push_back(result);
                }

                if (result.isConclusive() && !conclusive_found.load()) {
                    conclusive_found.store(true);

                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[" << name << "] Conclusive result: "
                                  << (result.status == AnalysisResult::TerminationStatus::TERMINATING
                                      ? "TERMINATING" : "NON-TERMINATING")
                                  << " (in " << result.execution_time_ms << "ms)\n";
                        std::cout << "[" << name << "] Cancelling other techniques...\n";
                    }

                    // Annuler toutes les autres techniques
                    for (size_t j = 0; j < techniques_.size(); ++j) {
                        if (j != i && techniques_[j]->canBeCancelled()) {
                            techniques_[j]->cancel();
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        final_result = result;
                    }
                } else if (!result.isConclusive() && verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[" << name << "] No conclusive result (took "
                              << result.execution_time_ms << "ms)\n";
                }

                return result;
            }
        ));
    }

    // Attendre tous les threads
    for (auto& f : futures) {
        try {
            f.get();
        } catch (...) {
            // Ignorer les exceptions des threads annulés
        }
    }

    if (!conclusive_found.load()) {
        if (verbose)
            std::cout << "\n=== All parallel techniques completed — result: UNKNOWN ===\n";
        final_result.technique_name = "None";
        final_result.description = "No proof found by any parallel technique";
    }

    return final_result;
}
