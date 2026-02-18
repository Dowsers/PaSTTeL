#ifndef NONLINEAR_MUL_HANDLER_H
#define NONLINEAR_MUL_HANDLER_H

#include "linearization/non_linear_term_handler.h"

/**
 * Handler for non-linear multiplication: (* expr1 expr2)
 * where neither expr1 nor expr2 is a pure numeric constant.
 *
 * Examples that ARE handled (non-linear):
 *   (* x y)          -> arith__mul__0
 *   (* (+ x 1) y)    -> arith__mul__1
 *
 * Examples that are NOT handled (linear - left for normal processing):
 *   (* 2 x)          -> kept as-is (coefficient * variable)
 *   (* x 2)          -> kept as-is (variable * coefficient)
 */
class NonLinearMultiplicationHandler : public NonLinearTermHandler {
public:
    NonLinearMultiplicationHandler() = default;

    bool canHandle(const std::string& op) const override;
    std::string getPrefix() const override;
    std::string getSort(const std::string& op,
                        const std::vector<std::string>& args) const override;
    std::string getName() const override;

    /**
     * Check if an expression is a pure numeric constant.
     * Returns true for: "123", "-45", "3.14", "-2.5"
     * Returns false for: "x", "(+ x 1)", "(* 2 y)"
     */
    static bool isNumericConstant(const std::string& expr);

    /**
     * Check if a multiplication should be linearized (both args are non-numeric).
     */
    static bool isNonLinearMultiplication(const std::vector<std::string>& args);
};

#endif // NONLINEAR_MUL_HANDLER_H
