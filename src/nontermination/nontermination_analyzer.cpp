#include <iostream>
#include <future>
#include <atomic>
#include <mutex>
#include <chrono>

#include "nontermination/nontermination_analyzer.h"
#include "nontermination/fixpoint_technique.h"
#include "nontermination/geometric_technique.h"
#include "pasttel.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

NonTerminationAnalyzer::NonTerminationAnalyzer() {
}

// ============================================================================
// GESTION DES TECHNIQUES
// ============================================================================

void NonTerminationAnalyzer::addTechnique(
    std::unique_ptr<NonTerminationTechniqueInterface> technique) {
    techniques_.push_back(std::move(technique));
}

void NonTerminationAnalyzer::clearTechniques() {
    techniques_.clear();
}

void NonTerminationAnalyzer::setDefaultTechniques() {
    clearTechniques();
    addTechnique(std::make_unique<FixpointTechnique>());
    addTechnique(std::make_unique<GeometricTechnique>());
}


// ============================================================================
// ANALYSE PRINCIPALE
// ============================================================================

NonTerminationResult NonTerminationAnalyzer::analyze(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    bool parallel) {

    // Cas trivial : pas de loop (0 polyèdres) → pas de non-terminaison possible
    if (lasso.hasNoLoop()) {
        NonTerminationResult result;
        result.type = NonTerminationResult::Type::UNKNOWN;
        result.is_nonterminating = false;
        result.description = "No loop body: nontermination is impossible.";
        result.technique_name = "EmptyLoop";
        all_results_.clear();
        all_results_.push_back(result);
        return result;
    }

    // Cas trivial : loop = "true" (1 polyèdre vide, aucune contrainte)
    // → la boucle s'exécute infiniment depuis n'importe quel état atteignable
    if (lasso.loop.isTrue()) {
        NonTerminationResult result;
        result.type = NonTerminationResult::Type::FIXPOINT;
        result.is_nonterminating = true;
        result.description = "Loop guard is 'true': the loop runs forever unconditionally.";
        result.technique_name = "TrivialFixpoint";
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

NonTerminationResult NonTerminationAnalyzer::analyzeSequential(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Nettoyer les résultats précédents
    all_results_.clear();

    if (verbose) {
        std::cout << "\n=== Sequential Nontermination Analysis ===" << std::endl;
        std::cout << "Techniques to try: " << techniques_.size() << std::endl;
    }

    // Essayer chaque technique séquentiellement
    for (size_t i = 0; i < techniques_.size(); ++i) {
        // Vérifier si l'analyse a été annulée de l'extérieur
        if (TERMINATION_FOUND.load()) {
            std::cout << "\n[Nontermination Analysis] ⊗ Cancelled - Termination proof found by other analysis" << std::endl;
            NonTerminationResult result;
            result.type = NonTerminationResult::Type::UNKNOWN;
            result.is_nonterminating = false;
            result.description = "Cancelled - Termination proof found";
            result.technique_name = "Cancelled";
            return result;
        }

        auto& technique = techniques_[i];

        if (verbose) {
            std::cout << "\n[Phase " << (i+1) << "/" << techniques_.size()
                        << "] Trying: " << technique->getName() << std::endl;
            std::cout << "  Description: " << technique->getDescription() << std::endl;
        }
        
        // Initialiser et analyser
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

        // Stocker le temps d'exécution et le nom de la technique
        tech_result.execution_time_ms = static_cast<double>(duration);
        tech_result.technique_name = technique->getName();

        // Collecter le résultat
        all_results_.push_back(tech_result);

        // Si preuve trouvée, retourner
        if (tech_result.is_nonterminating) {
            if (verbose) {
                std::cout << "  Nontermination proof found! (in "
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

    NonTerminationResult result;
    result.type = NonTerminationResult::Type::UNKNOWN;
    result.is_nonterminating = false;
    result.description = "No nontermination proof found by any technique";
    result.technique_name = "None";
    return result;
}

// ============================================================================
// ANALYSE PARALLÈLE AVEC EARLY STOPPING
// ============================================================================

NonTerminationResult NonTerminationAnalyzer::analyzeParallel(
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Nettoyer les résultats précédents
    all_results_.clear();

    std::cout << "\n=== Parallel Nontermination Analysis ===" << std::endl;
    std::cout << "Techniques running in parallel: " << techniques_.size() << std::endl;
    for (size_t i = 0; i < techniques_.size(); ++i) {
        std::cout << "  • Thread-" << i << ": " << techniques_[i]->getName() << std::endl;
    }
    std::cout << std::endl;

    // Flag partagé pour signaler qu'une preuve a été trouvée
    std::atomic<bool> proof_found{false};
    std::mutex result_mutex;
    NonTerminationResult final_result;

    // Pré-cloner les solvers avant le lancement des threads
    std::vector<std::shared_ptr<SMTSolver>> thread_solvers;
    thread_solvers.reserve(techniques_.size());
    thread_solvers.push_back(solver);  // Thread 0 : solver original
    for (size_t i = 1; i < techniques_.size(); ++i) {
        thread_solvers.push_back(solver->clone());
    }

    // Lancer toutes les techniques en parallèle
    std::vector<std::future<NonTerminationResult>> futures;

    for (size_t i = 0; i < techniques_.size(); ++i) {
        futures.push_back(std::async(std::launch::async,
            [&, i]() -> NonTerminationResult {
                auto& technique = techniques_[i];
                std::string technique_name = technique->getName();

                if (verbose) {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    std::cout << "[Thread-" << i << ": " << technique_name << "] Starting..." << std::endl;
                }

                // Vérifier si annulé avant de commencer
                if (TERMINATION_FOUND.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ⊗ Cancelled before start (termination found)" << std::endl;
                    }
                    return NonTerminationResult();
                }

                // Initialiser
                technique->init(lasso);

                if (!technique->validateConfiguration()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] Invalid configuration, skipping" << std::endl;
                    }
                    return NonTerminationResult();
                }

                // Analyser (interruptible si proof_found devient true)
                auto start_time = std::chrono::high_resolution_clock::now();

                // Utiliser le solver pré-cloné pour ce thread
                std::shared_ptr<SMTSolver> thread_solver = thread_solvers[i];

                auto result = technique->analyze(thread_solver);

                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time).count();

                // Stocker le temps d'exécution et le nom de la technique
                result.execution_time_ms = static_cast<double>(duration);
                result.technique_name = technique_name;

                // Collecter le résultat
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    all_results_.push_back(result);
                }

                // Si preuve trouvée
                if (result.is_nonterminating && !proof_found.load()) {
                    // Signaler aux autres threads
                    proof_found.store(true);

                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ✓ NON-TERMINATING - Proof found! (in "
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

                // Vérifier si annulé de l'extérieur (termination trouvée)
                if (TERMINATION_FOUND.load()) {
                    if (verbose) {
                        std::lock_guard<std::mutex> lock(result_mutex);
                        std::cout << "[Thread-" << i << ": " << technique_name << "] ⊗ Cancelled (termination found by other analysis)"
                                  << std::endl;
                    }
                    technique->cancel();
                }

                if (!result.is_nonterminating && verbose) {
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
    if (TERMINATION_FOUND.load()) {
        if (verbose) {
            std::cout << "\n=== Nontermination Analysis Cancelled ===" << std::endl;
            std::cout << "Result: Cancelled - Termination proof found by other analysis" << std::endl;
        }
        final_result.type = NonTerminationResult::Type::UNKNOWN;
        final_result.is_nonterminating = false;
        final_result.description = "Cancelled - Termination proof found";
        final_result.technique_name = "Cancelled";
        return final_result;
    }

    // Si aucune preuve trouvée
    if (!proof_found.load()) {
        if (verbose) {
            std::cout << "\n=== All parallel techniques completed ===" << std::endl;
            std::cout << "Result: No nontermination proof found" << std::endl;
            std::cout << "Techniques tried: ";
            for (size_t i = 0; i < techniques_.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << techniques_[i]->getName();
            }
            std::cout << std::endl;
        }

        final_result.type = NonTerminationResult::Type::UNKNOWN;
        final_result.is_nonterminating = false;
        final_result.description = "No nontermination proof found by any parallel technique";
        final_result.technique_name = "None";
    }

    return final_result;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void NonTerminationAnalyzer::printResult(const NonTerminationResult& result) const {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║            NONTERMINATION ANALYSIS RESULT                  ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    std::cout << "Non-terminating: "
            << (result.is_nonterminating ? "YES " : "NO ") << std::endl;

    std::cout << "Result type: ";
    switch (result.type) {
        case NonTerminationResult::Type::FIXPOINT:
            std::cout << "!  NON-TERMINATING (Fixpoint)" << std::endl;
            std::cout << "  Method: Fixpoint Check" << std::endl;
            std::cout << "\n  The program loops infinitely at a fixed state." << std::endl;
            
            if (!result.witness_state.empty()) {
                std::cout << "\n  Fixpoint state:" << std::endl;
                for (const auto& [var, val] : result.witness_state) {
                    std::cout << "    • " << var << " = " << val << std::endl;
                }
            }
            break;
        case NonTerminationResult::Type::GEOMETRIC_UNBOUNDED:
            std::cout << "!  NON-TERMINATING (Geometric Unbounded)" << std::endl;
            std::cout << "  Method: Geometric Nontermination Argument" << std::endl;
            std::cout << "\n  The program executes infinitely with geometric progression." << std::endl;
            
            if (!result.witness_state.empty()) {
                std::cout << "\n  Initial state (x₀):" << std::endl;
                for (const auto& [var, val] : result.witness_state) {
                    std::cout << "    • " << var << " = " << val << std::endl;
                }
                
                // std::cout << "\n  Eigenvector (v):" << std::endl;
                // if (!result.gnta_result.eigenvectors.empty()) {
                //     for (const auto& [var, val] : result.gnta_result.eigenvectors[0]) {
                //         std::cout << "    • v_" << var << " = " << val << std::endl;
                //     }
                // }
                
                // if (!result.gnta_result.lambdas.empty()) {
                //     std::cout << "\n  Eigenvalue (λ): " << result.gnta_result.lambdas[0] << std::endl;
                // }
            }
            break;
        case NonTerminationResult::Type::GEOMETRIC_FIXPOINT:
            std::cout << "!  NON-TERMINATING (Geometric Fixpoint)" << std::endl;
            std::cout << "  Method: Geometric Nontermination Argument" << std::endl;
            std::cout << "\n  The program loops infinitely at a geometric fixpoint." << std::endl;
            break;
        case NonTerminationResult::Type::UNKNOWN:
            std::cout << "  UNKNOWN" << std::endl;
            std::cout << "  Method: All analyses failed" << std::endl;
            std::cout << "\n  No nontermination proof found." << std::endl;
            std::cout << "  This does NOT prove the program terminates!" << std::endl;
            break;
    }
    std::cout << std::endl;

    std::cout << "\n  Description: " << result.description << std::endl;
}
