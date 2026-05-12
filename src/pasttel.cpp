#include <iostream>
#include <sstream>
#include <chrono>

#include "pasttel.h"
#include "parser/json_trace_parser.h"
#include "nontermination/fixpoint_technique.h"
#include "nontermination/geometric_technique.h"
#include "smtsolvers/SMTSolverZ3.h"
#include "smtsolvers/SMTSolverCVC5.h"



// ============================================================================
// CONFIGURATIONS GLOBALES
// ============================================================================
std::string FILENAME;

// Variables globales déclarées dans pasttel.h
AnalysisMode MODE = BOTH;
VerbosityLevel VERBOSITY = VerbosityLevel::NORMAL;
int CPUS = 1;
SolverType SOLVER = Z3;
NlaHandling NLA_HANDLING = NlaHandling::OVERAPPROXIMATE;
int TIMELIMIT = 6000;
bool verbose = false;
LinearMode LINEAR_MODE = LINEAR;

// Configurations par défaut pour les templates de ranking
std::vector<TemplateConfig> configs = {
    {0, 1, "(0, 1)"},
    // {1, 0, "(1, 0)"},
    // {0, 0, "(0, 0)"},
    // {0, 2, "(0, 2)"},
    // {1, 1, "(1, 1)"},
    // {2, 0, "(2, 0)"},
    // {2, 2, "(2, 2)"},
};

void printHelp(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <filename>\n\n"
                << "Options:\n"
                << "  -a <terminate|nonterminate|both>   Set analysis mode (default: both)\n"
                << "  -t <int>                           Time limit in seconds (default: 6000)\n"
                << "  -s <z3|cvc5>                       Set SMT solver (default: z3)\n"
                << "  -q                                 Quiet mode (silence output)\n"
                << "  -v                                 Verbose mode (more output)\n"
                << "  -c <int>                           Number of CPUs (default: 1)\n"
                << "  -nla <overapproximate|underapproximate|none>\n"
                << "                                     Non-linear arithmetic handling (default: overapproximate)\n"
                << "  -mode <linear|nonlinear>           Set analysis mode (default: linear)\n"
                << "  -h, --help                         Show this help message\n"
                << "\nExamples:\n"
                << "  " << programName << " -a terminate -s z3 -c 4 -t 300 input.json\n"
                << "  " << programName << " -a both -s cvc5 -v input.json\n";
}


// ============================================================================
// PARSING DES ARGUMENTS
// ============================================================================
std::string setParameters(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];

        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        else if (arg == "-a" && i + 1 < args.size()) {
            std::string val = args[++i];
            if (val == "terminate")         MODE = TERMINATION;
            else if (val == "nonterminate") MODE = NONTERMINATION;
            else if (val == "both")         MODE = BOTH;
            else {
                std::cerr << "Error: Invalid mode '" << val << "'. See --help.\n";
                std::exit(EXIT_FAILURE);
            }
        }
        else if (arg == "-t" && i + 1 < args.size()) {
            try {
                TIMELIMIT = std::stoi(args[++i]);
                if (TIMELIMIT < 1) throw std::invalid_argument("must be >= 1");
            } catch (...) {
                std::cerr << "Error: Invalid time limit. See --help.\n";
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
            verbose = false;
        }
        else if (arg == "-v") {
            VERBOSITY = VerbosityLevel::VERBOSE;
            verbose = true;
        }
        else if (arg == "-nla" && i + 1 < args.size()) {
            std::string val = args[++i];
            if      (val == "overapproximate")  NLA_HANDLING = NlaHandling::OVERAPPROXIMATE;
            else if (val == "underapproximate") NLA_HANDLING = NlaHandling::UNDERAPPROXIMATE;
            else if (val == "none")        NLA_HANDLING = NlaHandling::EXCEPTION;
            else {
                std::cerr << "Error: Invalid -nla mode '" << val << "'. See --help.\n";
                std::exit(EXIT_FAILURE);
            }
        }
        else if (arg == "-mode" && i + 1 < args.size()) {
            std::string val = args[++i];
            if (val == "linear")
                LINEAR_MODE = LINEAR;
            else if (val == "nonlinear") {
                LINEAR_MODE = NONLINEAR;
                std::cerr<<"Unsupported mode: Non-linear\n";
                exit(1);
            }
            else {
                std::cerr << "Error: Invalid -mode '" << val << "'. See --help.\n";
                std::exit(EXIT_FAILURE);
            }
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
                << std::setw(35) << "Technique"
                << std::setw(15) << "Result"
                << std::setw(12) << "Time (s)"
                << "Proof\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& result : report.termination_results) {
            bool is_terminating = (result.status == AnalysisResult::TERMINATING);
            std::cout << std::left
                    << std::setw(35) << result.technique_name
                    << std::setw(15) << (is_terminating ? "TERMINATING" : "UNKNOWN")
                    << std::setw(12) << std::fixed << std::setprecision(3) << (result.execution_time_ms / 1000.0);

            if (is_terminating && !result.proof_details.empty()) {
                std::string proof = result.proof_details;
                std::istringstream stream(proof);
                std::string line;

                bool first = true;
                while (std::getline(stream, line)) {

                    if (!first) {
                        std::cout << "\n"
                                << std::setw(35) << ""
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
                  << std::setw(35) << "Technique"
                  << std::setw(15) << "Result"
                  << std::setw(12) << "Time (s)"
                  << "Proof\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& result : report.nontermination_results) {
            bool is_nonterminating = (result.status == AnalysisResult::NON_TERMINATING);
            std::cout << std::left
                    << std::setw(35) << result.technique_name
                    << std::setw(15) << (is_nonterminating ? "NON-TERM" : "UNKNOWN")
                    << std::setw(12) << std::fixed << std::setprecision(3)
                    << (result.execution_time_ms / 1000.0);

            if (is_nonterminating && !result.proof_details.empty()) {

                std::string proof = result.proof_details;
                std::istringstream stream(proof);
                std::string line;

                bool first = true;
                while (std::getline(stream, line)) {

                    if (!first) {
                        std::cout << "\n"
                                << std::setw(35) << ""
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


// ============================================================================
// CREATION SOLVER + ANALYSE PRINCIPALE
// ============================================================================
/**
 * @brief Créer le solver SMT approprié
 */
SMTSolverInterface* createSMTSolver() {
    if (SOLVER == Z3)   return new SMTSolverZ3(verbose);
    if (SOLVER == CVC5) return new SMTSolverCVC5(verbose);
    throw std::runtime_error("Unknown solver type");
}

AnalysisReport runAnalysis(LassoProgram& lasso) {
    PortfolioOrchestrator orchestrator(CPUS);

    // Non-termination techniques
    if (MODE == NONTERMINATION || MODE == BOTH) {
        orchestrator.addTechnique(std::make_unique<FixpointTechnique>(createSMTSolver()));
        if(LINEAR_MODE == LINEAR){
            orchestrator.addTechnique(std::make_unique<GeometricTechnique>(createSMTSolver(),
                GeometricNonTerminationSettings{NUM_GEVS, true, true, GeometricNonTerminationSettings::AnalysisType::LINEAR}));
        }
        else{
            orchestrator.addTechnique(std::make_unique<GeometricTechnique>(createSMTSolver(),
                GeometricNonTerminationSettings{NUM_GEVS, true, true, GeometricNonTerminationSettings::AnalysisType::NONLINEAR}));
        }
    }

    // Termination techniques
    if (MODE == TERMINATION || MODE == BOTH) {
        orchestrator.addTechnique(std::make_unique<RankingBasedTechnique>(createSMTSolver(),
            "AffineTemplate", configs));
        orchestrator.addTechnique(std::make_unique<RankingBasedTechnique>(createSMTSolver(),
            "NestedTemplate", configs, 2));
        orchestrator.addTechnique(std::make_unique<RankingBasedTechnique>(createSMTSolver(),
            "NestedTemplate", configs, 3));
        orchestrator.addTechnique(std::make_unique<RankingBasedTechnique>(createSMTSolver(),
            "NestedTemplate", configs, 4));
    }

    orchestrator.solve(lasso);
    return orchestrator.join(TIMELIMIT);
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================
int main(int argc, char** argv) {

    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }

    std::string lasso_file = setParameters(argc, argv);
    LassoProgram lasso;
    try{
        lasso = JsonTraceParser::parseToLasso(lasso_file, true);
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to parse Lasso file: " << e.what() << std::endl;
        return 1;
    }

    // for debugging: print the parsed lasso
    if (verbose) {
        for(auto& v : lasso.program_vars)
            std::cout << "Program vars: " << v << " ";
        std::cout<< "\n=== STEM SMT ===\n";
        std::cout << lasso.stem.toSMTLib2() << std::endl;
        std::cout<< "\n=== LOOP SMT ===\n";
        std::cout << lasso.loop.toSMTLib2() << std::endl;
    }

    auto total_start = std::chrono::high_resolution_clock::now();
    // Run the analysis portfolio and get the report
    AnalysisReport report = runAnalysis(lasso);
    auto total_end = std::chrono::high_resolution_clock::now();
    report.total_time_ms = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            total_end - total_start).count());

    // Print the analysis report
    printAnalysisReport(report);
    return 0;
}
