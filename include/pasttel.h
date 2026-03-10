#ifndef MAIN_H
#define MAIN_H

#include <memory>
#include <string>
#include <vector>

#include "utiles.h"
#include "nla_handling.h"
#include "termination/ranking_based_technique.h"
#include "portfolio_orchestrator.h"

#define NUM_COMPONENTS_NESTED 2
#define NUM_GEVS 3

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

// Configurations par défaut pour les templates de ranking
extern std::vector<TemplateConfig> configs;

// ============================================================================
// STRUCTURES DE RAPPORT
// ============================================================================

struct AnalysisReport {
    std::vector<AnalysisResult> termination_results;     // résultats TERMINATING/UNKNOWN des techniques de terminaison
    std::vector<AnalysisResult> nontermination_results;  // résultats NON_TERMINATING/UNKNOWN des techniques de non-terminaison
    std::string overall_result; // "TERMINATING", "NON-TERMINATING", "UNKNOWN"
    double total_time_ms = 0.0;
    double terminating_time_ms = 0.0;
    double nonterminating_time_ms = 0.0;

    AnalysisReport() : overall_result("UNKNOWN") {}
};

// ============================================================================
// FONCTIONS
// ============================================================================

AnalysisResult runAnalysis(const LassoProgram& lasso);

void printAnalysisReport(const AnalysisReport& report);

#endif // MAIN_H
