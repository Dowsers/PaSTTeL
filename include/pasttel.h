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
extern int TIMELIMIT;

// Configurations par défaut pour les templates de ranking
extern std::vector<TemplateConfig> configs;

// ============================================================================
// FONCTIONS
// ============================================================================

AnalysisReport runAnalysis(const LassoProgram& lasso);

void printAnalysisReport(const AnalysisReport& report);

#endif // MAIN_H
