#include <iostream>
#include <future>
#include <sstream>
#include <chrono>

#include "terminator.h"
#include "parser/json_trace_parser.h"
#include "templates/affine_template.h"
#include "templates/nested_template.h"
#include "templates/lexicographic_template.h"
#include "nontermination/fixpoint_technique.h"
#include "nontermination/geometric_technique.h"
#include "nontermination/nontermination_analyzer.h"
#include "termination/ranking_and_invariant_validator.h"
#include "termination/termination_analyzer.h"
#include "termination/ranking_based_technique.h"
#include "smtsolvers/SMTSolverZ3.h"
#include "smtsolvers/SMTSolverCVC5.h"


// ============================================================================
// CONFIGURATIONS GLOBALES
// ============================================================================
std::string FILENAME;

// Variables globales déclarées dans terminator.h
AnalysisMode MODE = BOTH;
VerbosityLevel VERBOSITY = VerbosityLevel::NORMAL;
int CPUS = 1;
SolverType SOLVER = Z3;

// Les flags atomiques TERMINATION_FOUND et NONTERMINATION_FOUND sont définis inline dans terminator.h
// Rapport d'analyse
AnalysisReport report;

// Configurations par défaut pour les templates de ranking
std::vector<TemplateConfig> configs = {
    {1, 0, "(1, 0)"},
    {0, 0, "(0, 0)"},
    {0, 1, "(0, 1)"},
    {0, 2, "(0, 2)"},
    {1, 1, "(1, 1)"},
    {2, 0, "(2, 0)"},
    {2, 2, "(2, 2)"},
};

void printHelp(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <filename>\n\n"
              << "Options:\n"
              << "  -t <terminate|nonterminate|both>   Set termination mode (default: both)\n"
              << "  -s <z3|cvc5>                      Set SMT solver (default: z3)\n"
              << "  -q                                Quiet mode (silence output)\n"
              << "  -v                                Verbose mode (more output)\n"
              << "  -c <int>                          Number of CPUs (default: 1)\n"
              << "  -h, --help                        Show this help message\n"
              << "\nExamples:\n"
              << "  " << programName << " -t terminate -s z3 -c 4 input.txt\n"
              << "  " << programName << " -t both -s cvc5 -v input.txt\n";
}

// ============================================================================
// AFFICHAGE DU RAPPORT D'ANALYSE
// ============================================================================

/**
 * @brief Affiche un tableau parsable des résultats d'analyse
 */
void printAnalysisReport(const AnalysisReport& report) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    ANALYSIS REPORT                         \n";
    std::cout << "============================================================\n\n";

    // Afficher les résultats de terminaison
    if (!report.termination_results.empty()) {
        std::cout << "--- TERMINATION TECHNIQUES ---\n";
        std::cout << std::left
                  << std::setw(30) << "Technique"
                  << std::setw(15) << "Result"
                  << std::setw(12) << "Time (s)"
                  << "Proof\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& result : report.termination_results) {
            std::cout << std::left
                      << std::setw(30) << result.technique_name
                      << std::setw(15) << (result.is_terminating ? "TERMINATING" : "UNKNOWN")
                      << std::setw(12) << std::fixed << std::setprecision(3) << (result.execution_time_ms / 1000.0);

            if (result.is_terminating && !result.proof_details.empty()) {
                std::string proof = result.proof_details;
                std::istringstream stream(proof);
                std::string line;

                bool first = true;
                while (std::getline(stream, line)) {

                    if (!first) {
                        // Réaligner sous les colonnes précédentes
                        std::cout << "\n"
                                << std::setw(30) << ""
                                << std::setw(15) << ""
                                << std::setw(12) << "";
                    }

                    std::cout << line;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    // Afficher les résultats de non-terminaison
    if (!report.nontermination_results.empty()) {
        std::cout << "--- NON-TERMINATION TECHNIQUES ---\n";
        std::cout << std::left
                  << std::setw(30) << "Technique"
                  << std::setw(15) << "Result"
                  << std::setw(12) << "Time (s)"
                  << "Proof\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& result : report.nontermination_results) {

            std::cout << std::left
                    << std::setw(30) << result.technique_name
                    << std::setw(15) << (result.is_nonterminating ? "NON-TERM" : "UNKNOWN")
                    << std::setw(12) << std::fixed << std::setprecision(3)
                    << (result.execution_time_ms / 1000.0);

            if (result.is_nonterminating && !result.proof_details.empty()) {

                std::string proof = result.proof_details;
                std::istringstream stream(proof);
                std::string line;

                bool first = true;
                while (std::getline(stream, line)) {

                    if (!first) {
                        // Réaligner sous les colonnes précédentes
                        std::cout << "\n"
                                << std::setw(30) << ""
                                << std::setw(15) << ""
                                << std::setw(12) << "";
                    }

                    std::cout << line;
                    first = false;
                }
            }

            std::cout << "\n";
        }
        std::cout << "\n";
    }

    // Afficher le résultat global
    std::cout << "============================================================\n";
    std::cout << "OVERALL RESULT: " << report.overall_result << "\n";
    std::cout << "TOTAL TIME: " << std::fixed << std::setprecision(3)
              << (report.total_time_ms / 1000.0) << " s\n";

    if (!report.termination_results.empty()) {
        std::cout << "TERMINATING TIME: " << std::fixed << std::setprecision(3)
                    << (report.terminating_time_ms / 1000.0) << " s\n";
    }
    if (!report.nontermination_results.empty()) {
        std::cout << "NON-TERMINATING TIME: " << std::fixed << std::setprecision(3)
                    << (report.nonterminating_time_ms / 1000.0) << " s\n";
    }
    std::cout << "============================================================\n";
}

std::string setParameters(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];

        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        else if (arg == "-t" && i + 1 < args.size()) {
            std::string val = args[++i];
            if (val == "terminate")        MODE = TERMINATION;
            else if (val == "nonterminate") MODE = NONTERMINATION;
            else if (val == "both")         MODE = BOTH;
            else {
                std::cerr << "Error: Invalid mode '" << val << "'. See --help.\n";
                std::exit(EXIT_FAILURE);
            }
        }
        else if (arg == "-s" && i + 1 < args.size()) {
            std::string val = args[++i];
            if (val == "z3")           SOLVER = Z3;
            else if (val == "cvc5")    SOLVER = CVC5;
            else {
                std::cerr << "Error: Invalid solver '" << val << "'. See --help.\n";
                std::exit(EXIT_FAILURE);
            }
        }
        else if (arg == "-q") {
            VERBOSITY = VerbosityLevel::QUIET;
        }
        else if (arg == "-v") {
            VERBOSITY = VerbosityLevel::VERBOSE;
        }
        else if (arg == "-c" && i + 1 < args.size()) {
            try {
                CPUS = std::stoi(args[++i]);
                if (CPUS < 1) throw std::invalid_argument("must be >= 1");
            } catch (...) {
                std::cerr << "Error: Invalid CPU count. See --help.\n";
                std::exit(EXIT_FAILURE);
            }
        }
        else if (arg.rfind("-", 0) == 0) {
            std::cerr << "Error: Unknown option '" << arg << "'. See --help.\n";
            std::exit(EXIT_FAILURE);
        }
        else if (FILENAME.empty()) {
            FILENAME = arg;
        }
        else {
            std::cerr << "Error: Unexpected argument '" << arg << "'. See --help.\n";
            std::exit(EXIT_FAILURE);
        }
    }

    if (FILENAME.empty()) {
        std::cerr << "Error: Missing filename. See --help.\n";
        std::exit(EXIT_FAILURE);
    }

    return FILENAME;
}


// ============================================================================
// FONCTIONS D'ANALYSE DE TERMINAISON ET VÉRIFICATION
// ============================================================================

/**
 * @brief Factory pour créer le solver SMT approprié
 * @param verbose Activer le mode verbose
 * @return Pointeur partagé vers le solver créé
 */
std::shared_ptr<SMTSolver> createSMTSolver(bool verbose) {
    std::shared_ptr<SMTSolver> solver;

    if (SOLVER == Z3) {
        solver = std::make_shared<SMTSolverZ3>(verbose);
    } else if (SOLVER == CVC5) {
        solver = std::make_shared<SMTSolverCVC5>(verbose);
    } else {
        throw std::runtime_error("Unknown solver type");
    }

    return solver;
}

TerminationResult checkTermination(const LassoProgram& lasso){
    // Créer l'analyseur de terminaison
    TerminationAnalyzer analyzer;

    // Ajouter une technique par template
    // AffineTemplate
    analyzer.addTechnique(std::make_unique<RankingBasedTechnique>(
        "AffineTemplate",     // Template
        configs,              // Configurations: (0,0), (1,0), (0,1), etc.
        NUM_COMPONENTS_NESTED // Ignoré pour AffineTemplate
    ));

    // NestedTemplate
    analyzer.addTechnique(std::make_unique<RankingBasedTechnique>(
        "NestedTemplate",     // Template
        configs,              // Configurations: (0,0), (1,0), (0,1), etc.
        NUM_COMPONENTS_NESTED // Utilisé pour NestedTemplate
    ));

    // LexicographicTemplate
    // analyzer.addTechnique(std::make_unique<RankingBasedTechnique>(
    //     "LexicographicTemplate",     // Template
    //     configs,              // Configurations: (0,0), (1,0), (0,1), etc.
    //     NUM_COMPONENTS_NESTED // Utilisé pour NestedTemplate
    // ));
    
    // Analyse (mode séquentiel si CPUS=1, parallèle sinon)
    auto solver = createSMTSolver(VERBOSITY == VerbosityLevel::VERBOSE);
    bool use_parallel = (CPUS > 1);

    auto result = analyzer.analyze(lasso, solver, use_parallel);

    // Collecter tous les résultats
    if(result.is_terminating){
        report.overall_result = "TERMINATING";
        report.termination_results = analyzer.getAllResults();
        TERMINATION_FOUND.store(true);
        report.terminating_time_ms = result.execution_time_ms;
    }
    return result;
}

// ============================================================================
// FONCTIONS D'ANALYSE DE NON-TERMINATION
// ============================================================================

NonTerminationResult checkNonTermination(const LassoProgram& lasso){
    // Créer l'analyseur de non-terminaison
    NonTerminationAnalyzer nt_analyzer;

    // Ajouter les techniques (Fixpoint et Geometric)
    nt_analyzer.addTechnique(std::make_unique<FixpointTechnique>());
    nt_analyzer.addTechnique(std::make_unique<GeometricTechnique>(
        GeometricNonTerminationSettings{NUM_GEVS, true, true}));

    // Analyse (mode séquentiel si CPUS=1, parallèle sinon)
    auto solver = createSMTSolver(VERBOSITY == VerbosityLevel::VERBOSE);
    bool use_parallel = (CPUS > 1);


    auto nt_result = nt_analyzer.analyze(lasso, solver, use_parallel);

    // Collecter tous les résultats
    if (nt_result.is_nonterminating) {
        report.overall_result = "NON-TERMINATING";
        report.nontermination_results = nt_analyzer.getAllResults();
        NONTERMINATION_FOUND.store(true);
        report.nonterminating_time_ms = nt_result.execution_time_ms;
    }
    return nt_result;
}

void checkBoth(const LassoProgram& lasso){
  // Réinitialiser les flags globaux de communication
    TERMINATION_FOUND.store(false);
    NONTERMINATION_FOUND.store(false);

    // Lancer les deux analyses en parallèle
    auto termination_future = std::async(std::launch::async, [&]() -> TerminationResult {
        return checkTermination(lasso);
    });

    auto nontermination_future = std::async(std::launch::async, [&]() -> NonTerminationResult {
        return checkNonTermination(lasso);
    });

    // Attendre les deux analyses
    auto term_analysis = termination_future.get();
    auto nonterm_analysis = nontermination_future.get();

    // Vérifier les résultats (priorité aux preuves définitives)
    if (term_analysis.is_terminating) {
        report.overall_result = "TERMINATING";
    }
    else if (nonterm_analysis.is_nonterminating) {
        report.overall_result = "NON-TERMINATING";
    }
    else {
        report.overall_result = "UNKNOWN";
    }
}

int main(int argc, char** argv) {

    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }

    std::string lasso_file = setParameters(argc, argv);
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Detect format by file extension
    LassoProgram lasso;

    // .json extension
    lasso = JsonTraceParser::parseToLasso(lasso_file);

    if (verbose) {
        for(auto& v : lasso.program_vars)
            std::cout << "Program vars: " << v << " ";
        std::cout<< "\n=== STEM SMT ===\n";
        std::cout << lasso.stem.toSMTLib2() << std::endl;

        std::cout<< "\n=== LOOP SMT ===\n";
        std::cout << lasso.loop.toSMTLib2() << std::endl;
    }

    auto total_start = std::chrono::high_resolution_clock::now();

    if (MODE == TERMINATION) {
        checkTermination(lasso);
    }
    else if(MODE == NONTERMINATION) {
        checkNonTermination(lasso);
    }
    else if (MODE == BOTH) {
        checkBoth(lasso);
    }

    // Calculer le temps total
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        total_end - total_start).count();
    report.total_time_ms = static_cast<double>(total_duration);

    // Afficher le rapport
    printAnalysisReport(report);

    return 0;
}
