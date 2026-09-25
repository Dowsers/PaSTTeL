#ifndef UTILES_H
#define UTILES_H

#include <iomanip>
#include <cmath>
#include <string>
#include <stdexcept>

enum VerbosityLevel {
    QUIET,
    NORMAL,
    VERBOSE
};


extern VerbosityLevel VERBOSITY;

// Thrown by any deeply nested, otherwise non-cancellable preprocessing step
// (SMTParser::distributeAND's DNF expansion; ArrayHandler::classifyIndices'
// own throwaway-solver queries -- both run entirely in plain C++, before any
// SMT solver call, so SMTSolverInterface::interrupt() can't reach them) once
// the technique's own cancellation flag -- threaded down explicitly as a
// `const std::atomic<bool>*` from RankingBasedTechnique::init() /
// GeometricTechnique::init() through LassoProgram::linearize(),
// JsonTraceParser::parseToLasso()/parseTransition(), ArrayHandler's
// constructor and SMTParser::parseFormulaToDNF()/distributeAND() -- is set.
// The flag is already set by PortfolioOrchestrator::cancelTechniques() both
// when the time limit is reached and as soon as another technique wins, so
// preprocessing stops the moment either happens, not just at -t.
// Distinguished from a plain unsupported-construct failure so callers
// (PortfolioOrchestrator::join()) can report a TIMEOUT rather than
// "not supported" when nothing else completes a run either.
class PreprocessingCancelledException : public std::runtime_error {
public:
    explicit PreprocessingCancelledException(const std::string& msg) : std::runtime_error(msg) {}
};


// Helper function to format a double for SMT-LIB2 output
// Avoids scientific notation for large integers
inline std::string formatNumber(double value) {
    // double intpart;
    // if (std::modf(value, &intpart) == 0.0 &&
    //     value >= static_cast<double>(std::numeric_limits<long long>::min()) &&
    //     value <= static_cast<double>(std::numeric_limits<long long>::max())) {
    //     return std::to_string(static_cast<long long>(value));
    // }
    // std::ostringstream oss;
    // oss << std::fixed << std::setprecision(6) << value;
    // return oss.str();
    return std::to_string(value);
}



struct FunctionAbstraction {
    std::string fresh_var;      // Variable fraiche (ex: "uf__keccak256__0")
    std::string original_call;  // Terme original SMT-LIB (ex: "(keccak256 v_x_1)")
    std::string function_name;  // Nom de l'operateur (ex: "keccak256")
    std::string sort;           // Type de retour (ex: "Int")
};


#endif // UTILES_H