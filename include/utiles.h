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

#endif // UTILES_H