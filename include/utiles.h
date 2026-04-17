#ifndef UTILES_H
#define UTILES_H

#include <iomanip>
#include <cmath>
#include <string>

enum VerbosityLevel {
    QUIET,
    NORMAL,
    VERBOSE
};


extern VerbosityLevel VERBOSITY;


// Helper function to format a double for SMT-LIB2 output
// Avoids scientific notation for large integers
inline std::string formatNumber(double value) {
    double intpart;
    if (std::modf(value, &intpart) == 0.0 &&
        value >= static_cast<double>(std::numeric_limits<long long>::min()) &&
        value <= static_cast<double>(std::numeric_limits<long long>::max())) {
        return std::to_string(static_cast<long long>(value));
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}



struct FunctionAbstraction {
    std::string fresh_var;      // Variable fraiche (ex: "uf__keccak256__0")
    std::string original_call;  // Terme original SMT-LIB (ex: "(keccak256 v_x_1)")
    std::string function_name;  // Nom de l'operateur (ex: "keccak256")
    std::string sort;           // Type de retour (ex: "Int")
};


#endif // UTILES_H