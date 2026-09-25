#ifndef MAIN_H
#define MAIN_H

#include <memory>
#include <string>
#include <vector>

#include "utiles.h"
#include "nla_handling.h"
#include "termination/ranking_based_technique.h"
#include "portfolio_orchestrator.h"

#define NUM_GEVS 3

// ============================================================================
// CONFIGURATIONS GLOBALES
// ============================================================================

enum AnalysisMode {
    TERMINATION,
    NONTERMINATION,
    BOTH
};

enum LinearMode {
    LINEAR,
    NONLINEAR
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
// Post-synthesis ranking-function/SI validator (opt-in via -val). When true,
// every synthesized ranking argument is re-checked with the SMT solver and the
// config is rejected if the certificate does not hold on the actual loop.
extern bool USE_RF_VALIDATOR;

// Restrict the termination portfolio to a single ranking template (opt-in via
// -only <affine|nested|lexicographic|multiphase|piecewise>). Empty = run all
// (default). Lets a specific template's own certificate be exercised without
// a faster technique winning the portfolio race and cancelling it first.
extern std::string ONLY_TEMPLATE;

// Configurations par défaut pour les templates de ranking
extern std::vector<TemplateConfig> configs;

// ============================================================================
// FONCTIONS
// ============================================================================

AnalysisReport runAnalysis(const LassoProgram& lasso);

void printAnalysisReport(const AnalysisReport& report);

#endif // MAIN_H
