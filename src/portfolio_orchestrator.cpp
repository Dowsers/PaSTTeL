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
    std::unique_ptr<AnalysisInterface> technique)
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

    try {
        technique.init(lasso);
    } catch (const PreprocessingCancelledException& e) {
        log(verbose, "[" + name + "] Exception during init: " + e.what());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!had_init_exception_.load()) {
                init_exception_message_ = e.what();
                init_exception_is_timeout_ = true;
            }
        }
        had_init_exception_.store(true);
        return;
    } catch (const std::exception& e) {
        log(verbose, "[" + name + "] Exception during init: " + e.what());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!had_init_exception_.load()) {
                init_exception_message_ = e.what();
                init_exception_is_timeout_ = false;
            }
        }
        had_init_exception_.store(true);
        return;
    }

    if (stop_early_.load(std::memory_order_relaxed)) {
        log(verbose, "[" + name + "] Skipped (conclusive result already found)");
        return;
    }

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

    // Record the result and, if it is the first conclusive one, publish it as
    // the winner -- both under mutex_, which join() snapshots under too. The
    // winner must be completely written before signalEarlyExit() below wakes
    // join(): signalling first let join() copy final_result_ while this thread
    // was still assigning it, yielding a winner whose leading fields (status,
    // technique_name, ...) were set but whose certificate (rf_witness,
    // ranking_functions, ...) was still empty, or an UNKNOWN winner despite a
    // conclusive technique.
    bool won = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        all_results_.push_back(proof);
        if (proof.isConclusive() && !conclusive_found_.load()) {
            conclusive_found_.store(true);
            stop_early_.store(true);
            final_result_ = proof;
            won = true;
        }
    }

    if (!proof.isConclusive()) {
        log(verbose, "[" + name + "] No conclusive result (" +
            std::to_string(elapsed_ms) + " ms)");
        return;
    }
    if (!won) return;

    log(verbose, "[" + name + "] Conclusive: " +
        (verdict == AnalysisResult::TERMINATING
            ? "TERMINATING" : "NON-TERMINATING") +
        " (" + std::to_string(elapsed_ms) + " ms)");

    cancelTechniques(i, verbose);

    // Wake join()'s wait now instead of leaving it blocked until every losing
    // technique finishes or the full time limit elapses. Under mutex_ and only
    // if join() hasn't closed the report yet: once it has (e.g. it timed out
    // concurrently), it may already have released pool_.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!closed_)
        pool_->signalEarlyExit();
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
    closed_ = false;
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
    bool all_done = true;

    if (timelimit_seconds > 0) {
        auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::seconds(timelimit_seconds);
        all_done = pool_->waitUntil(deadline);
    } else {
        pool_->waitAll();
    }

    // Snapshot the results and close the report, under the lock runTechnique()
    // publishes them with (see there): from here on, a technique finishing late
    // no longer writes all_results_/final_result_ nor touches pool_.
    AnalysisReport report;
    bool init_exception_is_timeout;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        report.winner      = final_result_;
        report.all_results = all_results_;
        init_exception_is_timeout = init_exception_is_timeout_;
    }
    const bool conclusive = report.winner.isConclusive();
    // Early return via signalEarlyExit() (a winner) isn't a timeout.
    const bool timed_out = !all_done && !conclusive;

    // Cancel remaining techniques on timeout
    if (timed_out) {
        log(verbose, "\n=== Time limit reached — cancelling remaining techniques ===");
        stop_early_.store(true);
        cancelTechniques(techniques_.size(), verbose);
    }

    // Winner found or giving up on timeout: stop waiting for stragglers
    // instead of blocking on them cooperatively noticing cancel().
    if (conclusive || timed_out) {
        pool_->killAll();
        // Leaked, not reset(): a detached thread may still be mid-flight
        // and touch the pool, so freeing it here would be a use-after-free.
        pool_.release();
    } else {
        pool_.reset();
    }

    if (!conclusive) {
        log(verbose, timed_out
            ? "\n=== Time limit reached — result: UNKNOWN ==="
            : "\n=== All techniques completed — result: UNKNOWN ===");
        report.winner.technique_name = "None";
        report.winner.description    = timed_out
            ? "Time limit reached"
            : "No proof found by any technique";
    }

    if (report.winner.status == AnalysisResult::TERMINATING) {
        report.overall_result       = "TERMINATING";
        report.terminating_time_ms  = report.winner.execution_time_ms;
    } else if (report.winner.status == AnalysisResult::NON_TERMINATING) {
        report.overall_result         = "NON-TERMINATING";
        report.nonterminating_time_ms = report.winner.execution_time_ms;
    } else if (report.all_results.empty() && had_init_exception_.load()) {
        report.overall_result = init_exception_is_timeout ? "TIMEOUT" : "NOT SUPPORTED";
    }

    for (const auto& t : techniques_)
        report.registered_techniques.push_back(t->getName());

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
