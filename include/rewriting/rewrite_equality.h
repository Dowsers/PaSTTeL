#ifndef REWRITE_EQUALITY_H
#define REWRITE_EQUALITY_H

#include <string>

#include "rewrite_handler.h"

/**
 * RewriteEquality - Syntactic rewriting of equality predicates.
 *
 *   (= a b)        -->  (and (<= a b) (>= a b))
 *   (not (= a b))  -->  (or (>= a (+ b 1)) (<= a (- b 1)))   [integer semantics]
 *
 */
class RewriteEquality : public RewriteTermHandler {
public:
    // integer_mode: if true, (not (= a b)) → non-strict integer form instead of strict real form.
    explicit RewriteEquality(bool integer_mode = false) : integer_mode_(integer_mode) {}

    bool canHandle(const std::string& op) const override;

    /**
     * Recursively rewrite (= a b) and (not (= a b)) subexpressions.
     * Returns the rewritten formula.
     */
    std::string rewrite(const std::string& formula) override;

    std::string getName() const override;

private:
    bool integer_mode_;
    std::string rewriteExpr(const std::string& expr);
};

#endif // REWRITE_EQUALITY_H
