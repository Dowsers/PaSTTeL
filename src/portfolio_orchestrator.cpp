#include <iostream>
#include <future>
#include <atomic>
#include <mutex>
#include <chrono>

#include "portfolio_orchestrator.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

PortfolioOrchestrator::PortfolioOrchestrator(int max_threads)
    : max_threads_(max_threads), sem_count_(max_threads) {}

void PortfolioOrchestrator::addTechnique(
    std::unique_ptr<AnalysisTechniqueInterface> technique) {
    techniques_.push_back(std::move(technique));
}

void PortfolioOrchestrator::solve(
    const LassoProgram& lasso) {

    all_results_.clear();
    futures_.clear();
    conclusive_found_.store(false);
    final_result_ = ProofCertificate{};

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

    // thread_solvers_ = prepareSolvers(solver, n);
    futures_.reserve(n);

    // Limit the number of threads
    // TODO: use a pool of threads instead
    sem_count_ = max_threads_;

    // Launch each technique in its own thread
    for (size_t i = 0; i < n; ++i) {
        futures_.push_back(std::async(std::launch::async,
            [this, &lasso, i, verbose]() -> ProofCertificate {

                auto& technique = techniques_[i];
                std::string name = technique->getName();

                // Acquire a slot (semaphore): blocks until a thread slot is available
                {
                    std::unique_lock<std::mutex> lock(sem_mutex_);
                    sem_cv_.wait(lock, [this] { return sem_count_ > 0; });
                    --sem_count_;
                }

                // Release of semaphore slot on exit
                struct SemRelease {
                    PortfolioOrchestrator* self;
                    ~SemRelease() {
                        std::lock_guard<std::mutex> lock(self->sem_mutex_);
                        ++self->sem_count_;
                        self->sem_cv_.notify_one();
                    }
                } sem_release{this};

                // Check if another thread has already found a conclusive result
                if (conclusive_found_.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex_);
                        std::cout << "[" << name << "] Skipped (result already found)\n";
                    }
                    ProofCertificate r;
                    r.technique_name = name;
                    return r;
                }

                technique->init(lasso);

                if (!technique->validateConfiguration()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex_);
                        std::cout << "[" << name << "] Invalid configuration, skipping\n";
                    }
                    ProofCertificate r;
                    r.technique_name = name;
                    return r;
                }

                // auto thread_solver = thread_solvers_[i];
                // thread_solver->reset();
                // lasso.declareSolverContext(thread_solver);

                if (verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex_);
                    std::cout << "[" << name << "] Starting...\n";
                }

                // Run the main analysis
                auto start = std::chrono::high_resolution_clock::now();
                AnalysisResult verdict;
                try {
                    verdict = technique->analyze();
                } catch (const std::exception& e) {
                    ProofCertificate r;
                    r.technique_name = name;
                    return r;
                }
                auto end = std::chrono::high_resolution_clock::now();

                // Get the proof certificate and execution time
                auto proof = technique->getProof();
                proof.execution_time_ms = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start).count());

                // Store the result in the shared vector
                {
                    std::lock_guard<std::mutex> lock(result_mutex_);
                    all_results_.push_back(proof);
                }

                // If its the first conclusive result, store it and cancel other techniques
                if (proof.isConclusive() && !conclusive_found_.load()) {
                    conclusive_found_.store(true);

                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex_);
                        std::cout << "[" << name << "] Conclusive result: "
                                << (verdict == AnalysisResult::TERMINATING
                                    ? "TERMINATING" : "NON-TERMINATING")
                                << " (in " << proof.execution_time_ms << "ms)\n";
                        std::cout << "[" << name << "] Cancelling other techniques...\n";
                    }

                    // Cancel other techniques if possible
                    for (size_t j = 0; j < techniques_.size(); ++j) {
                        if (j != i && techniques_[j]->canBeCancelled()) {
                            techniques_[j]->cancel();
                        }
                    }

                    // Lock final result
                    {
                        std::lock_guard<std::mutex> lock(result_mutex_);
                        final_result_ = proof;
                    }
                } else if (!proof.isConclusive() && verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex_);
                    std::cout << "[" << name << "] No conclusive result (took "
                            << proof.execution_time_ms << "ms)\n";
                }

                return proof;
            }
        ));
    }
}

AnalysisReport PortfolioOrchestrator::join(int timelimit_seconds) {
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    bool timed_out = false;

    if (timelimit_seconds > 0) {
        auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(timelimit_seconds);

        for (auto& f : futures_) {
            if (std::chrono::steady_clock::now() >= deadline) {
                timed_out = true;
                break;
            }
            f.wait_until(deadline);
        }

        if (!timed_out && std::chrono::steady_clock::now() >= deadline)
            timed_out = true;

        if (timed_out) {
            if (verbose)
                std::cout << "\n=== Time limit reached — cancelling remaining techniques ===\n";
            for (auto& t : techniques_)
                if (t->canBeCancelled()) t->cancel();
        }
    } else {
        for (auto& f : futures_) {
            f.get();
        }
    }

    // Collect results from futures already done (timeout path)
    for (auto& f : futures_) {
        if (f.valid() &&
            f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            f.get();
        }
    }

    if (!conclusive_found_.load()) {
        if (verbose) {
            if (timed_out)
                std::cout << "\n=== Time limit reached — result: UNKNOWN ===\n";
            else
                std::cout << "\n=== All techniques completed — result: UNKNOWN ===\n";
        }
        final_result_.technique_name = "None";
        final_result_.description = timed_out
            ? "Time limit reached"
            : "No proof found by any technique";
    }

    // Build the report
    AnalysisReport report;
    report.winner = final_result_;

    for (const auto& r : all_results_) {
        if (r.status == AnalysisResult::TERMINATING)
            report.termination_results.push_back(r);
        else if (r.status == AnalysisResult::NON_TERMINATING)
            report.nontermination_results.push_back(r);
    }

    if (final_result_.status == AnalysisResult::TERMINATING) {
        report.overall_result = "TERMINATING";
        report.terminating_time_ms = final_result_.execution_time_ms;
    } else if (final_result_.status == AnalysisResult::NON_TERMINATING) {
        report.overall_result = "NON-TERMINATING";
        report.nonterminating_time_ms = final_result_.execution_time_ms;
    }

    return report;
}
