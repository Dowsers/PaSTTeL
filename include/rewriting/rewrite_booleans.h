#ifndef REWRITE_BOOLEANS_H
#define REWRITE_BOOLEANS_H

#include <string>
#include <set>

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
class RewriteBooleans {
public:
    /**
     * Construct with the set of SSA variable names that are boolean.
     * Example: {"v_flag_12", "v_flag_13", "v_alarmTrain_24"}
     */
    explicit RewriteBooleans(const std::set<std::string>& bool_ssa_vars);

    /**
     * Rewrite all bare boolean variables in the formula.
     */
    std::string rewrite(const std::string& formula) const;

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
