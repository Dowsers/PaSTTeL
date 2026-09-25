#ifndef REWRITE_BOOLEANS_H
#define REWRITE_BOOLEANS_H

#include <string>
#include <set>

#include "rewriting/rewrite_handler.h"

/**
 * RewriteBooleans - Replace boolean variables with integer comparisons.
 *
 * Boolean variables are treated as integers where true = 1 and false = 0.
 *
 * Rewrites:
 *   bare boolean var `v`     -->  (>= v 1)     (meaning v is true)
 *   (not v)  where v is bool -->  (<= v 0)     (meaning v is false)
 *
 * Must be applied BEFORE RewriteEquality and FormulaLinearizer.
 *
 * Requires knowledge of which SSA variables are boolean.
 */
class RewriteBooleans : public RewriteTermHandler {
public:
    /**
     * Construct with the set of SSA variable names that are boolean.
     * Example: {"v_flag_12", "v_flag_13", "v_alarmTrain_24"}
     */
    RewriteBooleans(const std::set<std::string>& bool_ssa_vars);

    /**
     * Check if the operator is a first-order boolean operator (not, and, or, =>, xor) or a boolean variable.
     */
    bool canHandle(const std::string& op) const override;

    /**
     * Rewrite formula AND inject 0/1 bounds for each bool SSA var.
     *   ite(b, 1, 0) = r  =>  (b -> r=1) /\ (!b -> r=0)  =>  0 <= r <= 1
     * Here r == b_ssa, so we add (and (>= b_ssa 0) (<= b_ssa 1)) to the formula.
     */
    std::string rewrite(const std::string& formula) override;

    /**
     * Get the name of the handler.
     */
    std::string getName() const override;

private:
    std::set<std::string> m_bool_vars;

    /**
     * Recursively process an S-expression.
     */
    std::string rewriteExpr(const std::string& expr) const;

    /**
     * Check if a token is a known boolean SSA variable.
     */
    bool isBoolVar(const std::string& token) const;
};

#endif // REWRITE_BOOLEANS_H
