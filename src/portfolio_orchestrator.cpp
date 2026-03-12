#include <iostream>
#include <future>
#include <atomic>
#include <mutex>
#include <chrono>

#include "portfolio_orchestrator.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

PortfolioOrchestrator::PortfolioOrchestrator(int max_threads)
    : max_threads_(max_threads) {}

void PortfolioOrchestrator::addTechnique(
    std::unique_ptr<AnalysisTechniqueInterface> technique) {
    techniques_.push_back(std::move(technique));
}

const std::vector<ProofCertificate>& PortfolioOrchestrator::getAllResults() const {
    return all_results_;
}

std::vector<std::shared_ptr<SMTSolver>> PortfolioOrchestrator::prepareSolvers(
    std::shared_ptr<SMTSolver> solver, size_t count) const {

    std::vector<std::shared_ptr<SMTSolver>> solvers;
    solvers.reserve(count);
    solvers.push_back(solver);
    for (size_t i = 1; i < count; ++i) {
        solvers.push_back(solver->clone());
    }
    return solvers;
}

ProofCertificate PortfolioOrchestrator::solve(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    all_results_.clear();
    lasso.declareSolverContext(solver);

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    const size_t n = techniques_.size();
    const size_t n_threads = std::min(static_cast<size_t>(max_threads_), n);

    if (verbose) {
        std::cout << "\n=== Portfolio Analysis ("
                  << n_threads << " thread(s)) ===\n";
        for (size_t i = 0; i < n; ++i)
            std::cout << "  * " << techniques_[i]->getName() << "\n";
        std::cout << "\n";
    }

    auto thread_solvers = prepareSolvers(solver, n);

    std::atomic<bool> conclusive_found{false};
    std::mutex result_mutex;
    ProofCertificate final_result;

    std::vector<std::future<ProofCertificate>> futures;
    futures.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        futures.push_back(std::async(std::launch::async,
            [this, &lasso, &conclusive_found, &result_mutex, &final_result,
             i, &thread_solvers, verbose]() -> ProofCertificate {

                auto& technique = techniques_[i];
                std::string name = technique->getName();

                if (conclusive_found.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[" << name << "] Skipped (result already found)\n";
                    }
                    ProofCertificate r;
                    r.technique_name = name;
                    return r;
                }

                technique->init(lasso);

                if (!technique->validateConfiguration()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[" << name << "] Invalid configuration, skipping\n";
                    }
                    ProofCertificate r;
                    r.technique_name = name;
                    return r;
                }

                auto thread_solver = thread_solvers[i];
                thread_solver->reset();
                lasso.declareSolverContext(thread_solver);

                if (verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[" << name << "] Starting...\n";
                }

                auto start = std::chrono::high_resolution_clock::now();
                auto verdict = technique->analyze(thread_solver);
                auto end = std::chrono::high_resolution_clock::now();

                auto proof = technique->getProof();
                proof.execution_time_ms = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start).count());

                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    all_results_.push_back(proof);
                }

                if (proof.isConclusive() && !conclusive_found.load()) {
                    conclusive_found.store(true);

                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[" << name << "] Conclusive result: "
                                  << (verdict == AnalysisResult::TERMINATING
                                      ? "TERMINATING" : "NON-TERMINATING")
                                  << " (in " << proof.execution_time_ms << "ms)\n";
                        std::cout << "[" << name << "] Cancelling other techniques...\n";
                    }

                    for (size_t j = 0; j < techniques_.size(); ++j) {
                        if (j != i && techniques_[j]->canBeCancelled()) {
                            techniques_[j]->cancel();
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        final_result = proof;
                    }
                } else if (!proof.isConclusive() && verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[" << name << "] No conclusive result (took "
                              << proof.execution_time_ms << "ms)\n";
                }

                return proof;
            }
        ));
    }

    for (auto& f : futures) {
        try { f.get(); } catch (...) {}
    }

    if (!conclusive_found.load()) {
        if (verbose)
            std::cout << "\n=== All techniques completed — result: UNKNOWN ===\n";
        final_result.technique_name = "None";
        final_result.description = "No proof found by any technique";
    }

    return final_result;
}
