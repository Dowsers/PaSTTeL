#include <iostream>
#include <chrono>
#include <future>
#include <atomic>
#include <mutex>

#include "termination/termination_analyzer.h"
#include "termination/ranking_based_technique.h"
#include "pasttel.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;


// ============================================================================
// CONSTRUCTEUR
// ============================================================================

TerminationAnalyzer::TerminationAnalyzer() {
    // Pas de techniques par défaut au démarrage
    // L'utilisateur doit les configurer explicitement
}

// ============================================================================
// GESTION DES TECHNIQUES
// ============================================================================

void TerminationAnalyzer::addTechnique(
    std::unique_ptr<TerminationTechniqueInterface> technique) {
    techniques_.push_back(std::move(technique));
}

void TerminationAnalyzer::clearTechniques() {
    techniques_.clear();
}

void TerminationAnalyzer::setDefaultTechniques() {
    clearTechniques();

    // Configuration par défaut: une technique par template
    std::vector<TemplateConfig> configs = {
        {1, 0, "(1, 0)"},
        {0, 0, "(0, 0)"},
        {0, 1, "(0, 1)"},
    };

    // AffineTemplate
    addTechnique(std::make_unique<RankingBasedTechnique>(
        "AffineTemplate",
        configs,
        2  // NUM_COMPONENTS_NESTED (ignoré pour AffineTemplate)
    ));

    // NestedTemplate
    addTechnique(std::make_unique<RankingBasedTechnique>(
        "NestedTemplate",
        configs,
        2  // NUM_COMPONENTS_NESTED
    ));

    // LexicographicTemplate
    addTechnique(std::make_unique<RankingBasedTechnique>(
        "LexicographicTemplate",
        configs,
        2  // NUM_COMPONENTS_LEX
    ));
}

// ============================================================================
// ANALYSE PRINCIPALE
// ============================================================================

TerminationResult TerminationAnalyzer::analyze(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    bool parallel) {

    // Cas trivial : pas de loop → terminaison immédiate
    if (lasso.hasNoLoop()) {
        TerminationResult result;
        result.type = TerminationResult::Type::RANKING_BASED;
        result.is_terminating = true;
        result.description = "No loop body: the program terminates trivially after the stem.";
        result.technique_name = "EmptyLoop";
        result.proof_details = "No loop body: the program terminates trivially after the stem.";
        all_results_.clear();
        all_results_.push_back(result);
        return result;
    }

    if (techniques_.empty()) {
        std::cerr << "Warning: No techniques configured. Using defaults." << std::endl;
        setDefaultTechniques();
    }

    // Déclarer le contexte complet du lasso dans le solver
    // (constantes, fonctions, axiomes, variables SSA, abstractions)
    lasso.declareSolverContext(solver);

    if (parallel) {
        return analyzeParallel(lasso, solver);
    } else {
        return analyzeSequential(lasso, solver);
    }
}

// ============================================================================
// ANALYSE SÉQUENTIELLE
// ============================================================================

TerminationResult TerminationAnalyzer::analyzeSequential(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Nettoyer les résultats précédents
    all_results_.clear();

    if (verbose) {
        std::cout << "\n=== Sequential Termination Analysis ===" << std::endl;
        std::cout << "Techniques to try: " << techniques_.size() << std::endl;
    }

    // Essayer chaque technique séquentiellement
    for (size_t i = 0; i < techniques_.size(); ++i) {
        // Vérifier si l'analyse a été annulée de l'extérieur
        if (NONTERMINATION_FOUND.load()) {
            if (verbose) {
                std::cout << "\n[Termination Analysis] ⊗ Cancelled - Nontermination proof found by other analysis" << std::endl;
            }
            TerminationResult result;
            result.type = TerminationResult::Type::UNKNOWN;
            result.is_terminating = false;
            result.description = "Cancelled - Nontermination proof found";
            result.technique_name = "Cancelled";
            return result;
        }

        auto& technique = techniques_[i];

        if (verbose) {
            std::cout << "\n[Phase " << (i+1) << "/" << techniques_.size()
                      << "] Trying: " << technique->getName() << std::endl;
            std::cout << "  Description: " << technique->getDescription() << std::endl;
        }

        // Initialiser le template
        technique->init(lasso);

        if (!technique->validateConfiguration()) {
            if (verbose) {
                std::cout << "  Configuration invalid, skipping" << std::endl;
            }
            continue;
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        auto tech_result = technique->analyze(solver);
        auto end_time = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();

        // Stocker le temps d'exécution
        tech_result.execution_time_ms = static_cast<double>(duration);

        // Collecter le résultat
        all_results_.push_back(tech_result);

        // Si preuve trouvée, retourner
        if (tech_result.is_terminating) {
            if (verbose) {
                std::cout << "  Termination proof found! (in "
                          << duration << "ms)" << std::endl;
            }
            return tech_result;
        }

        if (verbose) {
            std::cout << "  No proof found (took " << duration << "ms)" << std::endl;
        }
    }

    // Aucune technique n'a trouvé de preuve
    if (verbose) {
        std::cout << "\n  All techniques failed - termination status unknown" << std::endl;
    }

    TerminationResult result;
    result.type = TerminationResult::Type::UNKNOWN;
    result.is_terminating = false;
    result.description = "No termination proof found by any technique";
    result.technique_name = "None";
    return result;
}

// ============================================================================
// ANALYSE PARALLÈLE AVEC EARLY STOPPING
// ============================================================================

TerminationResult TerminationAnalyzer::analyzeParallel(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Nettoyer les résultats précédents
    all_results_.clear();

    std::cout << "\n=== Parallel Termination Analysis ===" << std::endl;
    std::cout << "Techniques running in parallel: " << techniques_.size() << std::endl;
    for (size_t i = 0; i < techniques_.size(); ++i) {
        std::cout << "  • Thread-" << i << ": " << techniques_[i]->getName() << std::endl;
    }
    std::cout << std::endl;

    // Flag partagé pour signaler qu'une preuve a été trouvée
    std::atomic<bool> proof_found{false};
    std::mutex result_mutex;
    TerminationResult final_result;

    // Pré-cloner les solvers avant le lancement des threads
    std::vector<std::shared_ptr<SMTSolver>> thread_solvers;
    thread_solvers.reserve(techniques_.size());
    thread_solvers.push_back(solver);  // Thread 0 : solver original
    for (size_t i = 1; i < techniques_.size(); ++i) {
        thread_solvers.push_back(solver->clone());
    }

    // Lancer toutes les techniques en parallèle
    std::vector<std::future<TerminationResult>> futures;

    for (size_t i = 0; i < techniques_.size(); ++i) {
        futures.push_back(std::async(std::launch::async,
            [&, i]() -> TerminationResult {
                auto& technique = techniques_[i];
                std::string technique_name = technique->getName();

                if (verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[Thread-" << i << ": " << technique_name << "] Starting..." << std::endl;
                }

                // Vérifier si annulé avant de commencer
                if (NONTERMINATION_FOUND.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ⊗ Cancelled before start (nontermination found)" << std::endl;
                    }
                    return TerminationResult();
                }

                // Initialiser
                technique->init(lasso);

                if (!technique->validateConfiguration()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] Invalid configuration, skipping" << std::endl;
                    }
                    return TerminationResult();
                }

                // Analyser (interruptible si proof_found devient true)
                auto start_time = std::chrono::high_resolution_clock::now();

                // Utiliser le solver pré-cloné pour ce thread
                std::shared_ptr<SMTSolver> thread_solver = thread_solvers[i];

                auto result = technique->analyze(thread_solver);

                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time).count();

                // Stocker le temps d'exécution
                result.execution_time_ms = static_cast<double>(duration);

                // Collecter le résultat
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    all_results_.push_back(result);
                }

                // Si preuve trouvée
                if (result.is_terminating && !proof_found.load()) {
                    // Signaler aux autres threads
                    proof_found.store(true);

                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ✓ TERMINATING - Proof found! (in "
                                    << duration << "ms)" << std::endl;
                        std::cout << "[Thread-" << i << ": " << technique_name << "] Cancelling other techniques..."
                                    << std::endl;
                    }

                    // Annuler les autres techniques
                    for (size_t j = 0; j < techniques_.size(); ++j) {
                        if (j != i && techniques_[j]->canBeCancelled()) {
                            techniques_[j]->cancel();
                        }
                    }

                    // Sauvegarder le résultat
                    {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        final_result = result;
                    }

                    return result;
                }

                // Vérifier si une autre technique a trouvé
                if (proof_found.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ⊗ Cancelled (another thread found proof)"
                                  << std::endl;
                    }
                    technique->cancel();
                }

                // Vérifier si annulé de l'extérieur (nontermination trouvée)
                if (NONTERMINATION_FOUND.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ⊗ Cancelled (nontermination found by other analysis)"
                                  << std::endl;
                    }
                    technique->cancel();
                }

                if (!result.is_terminating && verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[Thread-" << i << ": " << technique_name << "] ✗ No proof found (took "
                              << duration << "ms)" << std::endl;
                }

                return result;
            }
        ));
    }

    // Attendre que tous les threads se terminent
    for (auto& future : futures) {
        try {
            future.get();  // Force complete resource cleanup
        } catch (...) {
            // Ignorer les exceptions
        }
    }

    // Vérifier si annulé de l'extérieur
    if (NONTERMINATION_FOUND.load()) {
        if (verbose) {
            std::cout << "\n=== Termination Analysis Cancelled ===" << std::endl;
            std::cout << "Result: Cancelled - Nontermination proof found by other analysis" << std::endl;
        }
        final_result.type = TerminationResult::Type::UNKNOWN;
        final_result.is_terminating = false;
        final_result.description = "Cancelled - Nontermination proof found";
        final_result.technique_name = "Cancelled";
        return final_result;
    }

    // Si aucune preuve trouvée
    if (!proof_found.load()) {
        if (verbose) {
            std::cout << "\n=== All parallel techniques completed ===" << std::endl;
            std::cout << "Result: No termination proof found" << std::endl;
            std::cout << "Techniques tried: ";
            for (size_t i = 0; i < techniques_.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << techniques_[i]->getName();
            }
            std::cout << std::endl;
        }

        final_result.type = TerminationResult::Type::UNKNOWN;
        final_result.is_terminating = false;
        final_result.description = "No termination proof found by any parallel technique";
        final_result.technique_name = "None";
    }

    return final_result;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void TerminationAnalyzer::printResult(const TerminationResult& result) const {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║            TERMINATION ANALYSIS RESULT                     ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;

    std::cout << "Terminating: "
              << (result.is_terminating ? "YES " : "NO ") << std::endl;

    std::cout << "Result type: ";
    switch (result.type) {
        case TerminationResult::Type::RANKING_BASED:
            std::cout << "TERMINATING (Ranking Function)" << std::endl;
            std::cout << "  Method: " << result.technique_name << std::endl;
            std::cout << "\n  A ranking function was found proving termination." << std::endl;

            if (!result.witness.empty()) {
                std::cout << "\n  Ranking function coefficients:" << std::endl;
                for (const auto& [var, val] : result.witness) {
                    std::cout << "    • " << var << " = " << val << std::endl;
                }
            }
            break;

        case TerminationResult::Type::UNKNOWN:
            std::cout << "UNKNOWN" << std::endl;
            std::cout << "  Method: All analyses failed" << std::endl;
            std::cout << "\n  No termination proof found." << std::endl;
            std::cout << "  This does NOT prove the program is non-terminating!" << std::endl;
            break;
    }
    std::cout << std::endl;

    std::cout << "\n  Description: " << result.description << std::endl;
}
