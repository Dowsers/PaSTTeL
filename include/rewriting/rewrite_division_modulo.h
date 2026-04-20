#ifndef REWRITE_DIVISION_H
#define REWRITE_DIVISION_H

#include <string>
#include <vector>
#include <map>

#include "linearization/formula_linearizer.h"  // for FunctionAbstraction
#include "parser/sexpr_utils.h"
#include "rewriting/formula_rewriter.h"

/**
 * RewriteDivisionMod - Replace integer division and modulo by auxiliary variables
 *                   with equivalent linear constraints.
 * For (div dividend divisor), introduce fresh variable q and constraints:
 *    divisor >= 1 & dividend IN [q*divisor, (q+1)*divisor - 1]
 * OR
 *    divisor <= -1 & dividend IN [(q-1)*divisor - 1, q*divisor]
 *
 * For (mod dividend divisor), introduce fresh variable r and constraints:
 *    divisor >= 1 & r IN [0, divisor-1] & dividend = q*divisor + r
 * OR
 *    divisor <= -1 & r IN [0, (-divisor)-1] & dividend = q*divisor + r
 *
 * RewriteDivisionMod:
 *
 *   (div dividend divisor) is replaced by fresh variable q, with constraints:
 *     (or
 *       (and (>= divisor 1)
 *            (<= (* q divisor) dividend)
 *            (<= dividend (- (* (+ q 1) divisor) 1)))
 *       (and (<= divisor (- 0 1))
 *            (<= (* q divisor) dividend)
 *            (<= dividend (- (* (- q 1) divisor) 1))))
 *
 *   (mod dividend divisor) is replaced by fresh variable r, with constraints:
 *     (or
 *       (and (>= divisor 1)
 *            (<= 0 r) (<= r (- divisor 1))
 *            (= dividend (+ (* q divisor) r)))
 *       (and (<= divisor (- 0 1))
 *            (<= 0 r) (<= r (- (- 0 divisor) 1))
 *            (= dividend (+ (* q divisor) r))))
 *
 * The auxiliary constraints are conjoined directly into the formula
 * (not added as separate solver assertions).
 *
 * Uses SMTLIB2 semantics where the remainder is always positive.
 */
class RewriteDivisionMod : public RewriteTermHandler {
public:
    RewriteDivisionMod();

    bool canHandle(const std::string& op) const override;

    /**
     * Rewrite all (div ...) and (mod ...) subexpressions in the formula.
     * Returns the rewritten formula with auxiliary constraints conjoined.
     */
    std::string rewrite(const std::string& formula) override;

    std::string getName() const override;

    /**
     * Get the auxiliary variables created during rewriting.
     * These need to be declared in the solver (as Int).
     * Stored as FunctionAbstraction with empty original_call.
     */
    std::vector<FunctionAbstraction> getAuxVars() const override;

    /**
     * Reset state (clear aux vars and caches).
     */
    void reset();

private:
    int m_counter;

    // Auxiliary variables created (fresh_var + sort, original_call empty)
    std::vector<FunctionAbstraction> m_aux_vars;

    // Auxiliary constraints to conjoin to the formula
    std::vector<std::string> m_aux_constraints;

    // Cache: normalized (div x y) or (mod x y) string -> fresh var name
    std::map<std::string, std::string> m_div_cache;
    std::map<std::string, std::string> m_mod_cache;

    /**
     * Recursively rewrite an S-expression, replacing div/mod subterms.
     */
    std::string rewriteExpr(const std::string& expr);

    /**
     * Create a fresh auxiliary variable name with the given prefix.
     */
    std::string createFreshVar(const std::string& prefix);

    /**
     * Get or create the quotient aux var for (div dividend divisor).
     * Also generates the auxiliary linear constraints.
     */
    std::string getOrCreateDivVar(const std::string& dividend,
                                   const std::string& divisor);

    /**
     * Get or create the remainder aux var for (mod dividend divisor).
     * Also creates the quotient var if needed, and generates constraints.
     */
    std::string getOrCreateModVar(const std::string& dividend,
                                   const std::string& divisor);

    /**
     * Split S-expression into top-level tokens.
     */
    std::vector<std::string> splitSExpr(const std::string& expr) {
        return SExprUtils::splitSExpr(expr);
    }

    /**
     * Returns true if s is a positive integer literal (e.g. "256", "1").
     * Sets value to the parsed integer. Used to detect constant divisors
     * and avoid generating spurious disjunctions in div/mod constraints.
     */
    bool isPositiveIntLiteral(const std::string& s, long long& value);
};

#endif // REWRITE_DIVISION_H
