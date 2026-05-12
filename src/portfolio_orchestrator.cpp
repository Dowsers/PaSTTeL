#include <iostream>
#include <future>
#include <atomic>
#include <mutex>
#include <chrono>

#include "portfolio_orchestrator.h"
#include "thread_pool.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ─────────────────────────────────────────────
//  Construction / technique registration
// ─────────────────────────────────────────────

PortfolioOrchestrator::PortfolioOrchestrator(int max_threads)
    : max_threads_(max_threads) {}

void PortfolioOrchestrator::addTechnique(
    std::unique_ptr<AnalysisTechniqueInterface> technique)
{
    if (technique->requiresLinearization())
        // insert at the end to prioritize techniques that can run on the raw lasso
        techniques_.push_back(std::move(technique));
    else
        // insert at the beginning to prioritize un-processed lassos
        techniques_.insert(techniques_.begin(), std::move(technique));
}

// ─────────────────────────────────────────────
//  Core: run one technique
// ─────────────────────────────────────────────

void PortfolioOrchestrator::runTechnique(size_t i, const LassoProgram& lasso)
{
    auto& technique   = *techniques_[i];
    const auto name   = technique.getName();
    const bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Early exit: conclusive result found OR time limit reached
    if (stop_early_.load(std::memory_order_relaxed)) {
        log(verbose, "[" + name + "] Skipped");
        return;
    }

    technique.init(lasso);

    if (!technique.validateConfiguration()) {
        log(verbose, "[" + name + "] Invalid configuration, skipping");
        return;
    }

    log(verbose, "[" + name + "] Starting...");

    // Run the analysis and time it
    auto start = std::chrono::high_resolution_clock::now();
    AnalysisResult verdict;
    try {
        verdict = technique.analyze();
    } catch (const std::exception& e) {
        log(verbose, "[" + name + "] Exception: " + e.what());
        return;
    }
    auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - start).count();

    auto proof              = technique.getProof();
    proof.technique_name    = name;
    proof.execution_time_ms = elapsed_ms;

    // Store result (mutex only needed here and for final_result_)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        all_results_.push_back(proof);
    }

    if (proof.isConclusive()) {
        // atomic exchange: only the first conclusive result wins
        bool expected = false;
        if (conclusive_found_.compare_exchange_strong(expected, true)) {
            stop_early_.store(true);

            log(verbose, "[" + name + "] Conclusive: " +
                (verdict == AnalysisResult::TERMINATING
                    ? "TERMINATING" : "NON-TERMINATING") +
                " (" + std::to_string(elapsed_ms) + " ms)");

            cancelTechniques(i, verbose);

            std::lock_guard<std::mutex> lock(mutex_);
            final_result_ = proof;
        }
    } else {
        log(verbose, "[" + name + "] No conclusive result (" +
            std::to_string(elapsed_ms) + " ms)");
    }
}


void PortfolioOrchestrator::cancelTechniques(size_t winner, bool verbose)
{
    if(winner < techniques_.size())
        log(verbose, "[" + techniques_[winner]->getName()
            + "] Cancelling other techniques...");
    for (size_t j = 0; j < techniques_.size(); ++j)
        if (j != winner && techniques_[j]->canBeCancelled())
            techniques_[j]->cancel();
}


// ─────────────────────────────────────────────
//  solve(): enqueue all techniques in order
// ─────────────────────────────────────────────

void PortfolioOrchestrator::solve(LassoProgram& lasso)
{
    all_results_.clear();
    final_result_ = {};
    conclusive_found_.store(false);
    stop_early_.store(false);

    const bool verbose  = (VERBOSITY == VerbosityLevel::VERBOSE);
    const size_t n      = techniques_.size();
    const size_t n_threads = std::min(static_cast<size_t>(max_threads_), n);

    if (verbose) {
        std::cout << "\n=== Portfolio Analysis (" << n_threads << " thread(s)) ===\n";
        for (const auto& t : techniques_)
            std::cout << "  * " << t->getName() << "\n";
        std::cout << "\n";
    }

    // Build the pool lazily so its lifetime matches the solve/join pair.
    // Tasks are enqueued in index order:.
    pool_ = std::make_unique<ThreadPool>(n_threads);
    for (size_t i = 0; i < n; ++i)
        pool_->enqueue(std::bind(&PortfolioOrchestrator::runTechnique, this, i,
                                std::cref(lasso)));
}


// ─────────────────────────────────────────────
//  join(): wait (with optional time limit)
// ─────────────────────────────────────────────

AnalysisReport PortfolioOrchestrator::join(int timelimit_seconds)
{
    const bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    bool timed_out = false;

    if (timelimit_seconds > 0) {
        auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(timelimit_seconds);
        timed_out = !pool_->waitUntil(deadline);
    } else {
        pool_->waitAll();
    }

    // Cancel remaining techniques on timeout
    if (timed_out) {
        log(verbose, "\n=== Time limit reached — cancelling remaining techniques ===");
        stop_early_.store(true);
        cancelTechniques(techniques_.size(), verbose);
        pool_->waitAll();
    }

    pool_.reset(); // destroy the pool (joins all workers)

    if (!conclusive_found_.load()) {
        log(verbose, timed_out
            ? "\n=== Time limit reached — result: UNKNOWN ==="
            : "\n=== All techniques completed — result: UNKNOWN ===");
        final_result_.technique_name = "None";
        final_result_.description    = timed_out
            ? "Time limit reached"
            : "No proof found by any technique";
    }

    // Build report
    AnalysisReport report;
    report.winner = final_result_;

    for (const auto& r : all_results_) {
        if      (r.status == AnalysisResult::TERMINATING)
            report.termination_results.push_back(r);
        else if (r.status == AnalysisResult::NON_TERMINATING)
            report.nontermination_results.push_back(r);
    }

    if (final_result_.status == AnalysisResult::TERMINATING) {
        report.overall_result       = "TERMINATING";
        report.terminating_time_ms  = final_result_.execution_time_ms;
    } else if (final_result_.status == AnalysisResult::NON_TERMINATING) {
        report.overall_result         = "NON-TERMINATING";
        report.nonterminating_time_ms = final_result_.execution_time_ms;
    }

    return report;
}

// ─────────────────────────────────────────────
//  Helper
// ─────────────────────────────────────────────

void PortfolioOrchestrator::log(bool verbose, const std::string& msg) const
{
    if (!verbose) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << msg << "\n";
}
