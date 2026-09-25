#include "linearization/nonlinear_mul_handler.h"
#include <regex>

bool NonLinearMultiplicationHandler::canHandle(const std::string& op) const {
    return op == "*";
}

std::string NonLinearMultiplicationHandler::getPrefix() const {
    return "arith__";
}

std::string NonLinearMultiplicationHandler::getSort(const std::string& /*op*/,
                                                     const std::vector<std::string>& /*args*/) const {
    // Multiplication returns Int (we assume integer arithmetic)
    return "Int";
}

std::string NonLinearMultiplicationHandler::getName() const {
    return "NonLinearMultiplicationHandler";
}

bool NonLinearMultiplicationHandler::isNumericConstant(const std::string& expr) {
    if (expr.empty()) return false;

    // Trim whitespace
    std::string trimmed = expr;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    size_t end = trimmed.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    trimmed = trimmed.substr(start, end - start + 1);

    // A numeric constant matches: optional minus, digits, optional decimal part
    // Pattern: -?[0-9]+(\.[0-9]+)?
    static const std::regex numeric_regex("^-?[0-9]+(\\.[0-9]+)?$");
    return std::regex_match(trimmed, numeric_regex);
}

bool NonLinearMultiplicationHandler::isNonLinearMultiplication(const std::vector<std::string>& args) {
    // A multiplication is non-linear if BOTH arguments are non-numeric
    if (args.size() != 2) return false;

    bool arg1_is_numeric = isNumericConstant(args[0]);
    bool arg2_is_numeric = isNumericConstant(args[1]);

    // Non-linear: neither is a number (e.g., (* x y) or (* (+ x 1) y))
    return !arg1_is_numeric && !arg2_is_numeric;
}
