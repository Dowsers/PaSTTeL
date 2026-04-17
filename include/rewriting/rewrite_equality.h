#ifndef REWRITE_EQUALITY_H
#define REWRITE_EQUALITY_H

#include <string>

#include "rewrite_handler.h"

/**
 * RewriteEquality - Syntactic rewriting of equality predicates.
 *
 *   (= a b)  -->  (and (<= a b) (>= a b))
 *
 */
class RewriteEquality : public RewriteTermHandler {
public:

    bool canHandle(const std::string& op) const override;

    /**
     * Recursively rewrite all (= a b) subexpressions in the formula.
     * Returns the rewritten formula.
     */
    std::string rewrite(const std::string& formula) override;

    std::string getName() const override;

private:
    /**
     * Recursively process an S-expression.
     */
    std::string rewriteExpr(const std::string& expr);
};

#endif // REWRITE_EQUALITY_H
