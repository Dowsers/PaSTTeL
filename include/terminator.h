#ifndef MAIN_H
#define MAIN_H

#include <memory>
#include <string>
#include <vector>
#include <atomic>

#include "utiles.h"
#include "termination/generic_termination_synthesizer.h"
#include "termination/termination_technique_interface.h"
#include "nontermination/nontermination_analyzer.h"
#include "termination/ranking_based_technique.h"

#define NUM_COMPONENTS_NESTED 2 // Nombre de composantes pour le Nested Template
#define NUM_GEVS 3 // Nombre d'exécutions géométriques à essayer 

// ============================================================================
// CONFIGURATIONS GLOBALES
// ============================================================================

enum AnalysisMode {
    TERMINATION,
    NONTERMINATION,
    BOTH
};

enum SolverType {
    Z3,
    CVC5
};

extern AnalysisMode MODE;
extern VerbosityLevel VERBOSITY;
extern int CPUS;
extern SolverType SOLVER;

// Flags de communication entre analyses (pour mode BOTH)
// Définis inline car utilisés par les tests qui ne linkent pas terminator.o
inline std::atomic<bool> TERMINATION_FOUND{false};
inline std::atomic<bool> NONTERMINATION_FOUND{false};

// Configurations par défaut pour les templates de ranking
extern std::vector<TemplateConfig> configs;

// ============================================================================
// STRUCTURES DE RAPPORT
// ============================================================================

/**
 * @brief Structure pour collecter tous les résultats d'analyse
 */
struct AnalysisReport {
    std::vector<TerminationResult> termination_results;
    std::vector<NonTerminationResult> nontermination_results;
    std::string overall_result; // "TERMINATING", "NON-TERMINATING", "UNKNOWN"
    double total_time_ms;
    double terminating_time_ms;
    double nonterminating_time_ms;

    AnalysisReport() : overall_result("UNKNOWN"), total_time_ms(0.0), terminating_time_ms(0.0), nonterminating_time_ms(0.0) {}
};

// ============================================================================
// FONCTIONS
// ============================================================================

TerminationResult checkTermination(const LassoProgram& lasso);
NonTerminationResult checkNonTermination(const LassoProgram& lasso);

/**
 * @brief Affiche le tableau des résultats de manière parsable
 */
void printAnalysisReport(const AnalysisReport& report);

#endif // MAIN_H
