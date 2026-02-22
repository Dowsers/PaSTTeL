#ifndef REWRITE_EQUALITY_H
#define REWRITE_EQUALITY_H

#include <string>

/**
 * RewriteEquality - Syntactic rewriting of equality predicates.
 *
 *   (= a b)  -->  (and (<= a b) (>= a b))
 *
 * This is applied recursively on the S-expression BEFORE parsing to DNF,
 * so that SMTParser only needs to handle inequality operators (<=, >=, <, >).
 *
 * Stateless: all methods are static.
 */
class RewriteEquality {
public:
    /**
     * Recursively rewrite all (= a b) subexpressions in the formula.
     * Returns the rewritten formula.
     */
    static std::string rewrite(const std::string& formula);

private:
    /**
     * Recursively process an S-expression.
     */
    static std::string rewriteExpr(const std::string& expr);
};

#endif // REWRITE_EQUALITY_H
