#ifndef REWRITE_LET_H
#define REWRITE_LET_H

#include <string>

#include "rewrite_handler.h"

/**
 * RewriteLet - Inline (expand) let bindings in SMT-LIB2 formulas.
 *
 *   (let ((.cse0 (+ x 1)) (.cse1 (- y 2)))
 *     (and (= .cse0 y) (>= .cse1 0)))
 *   -->
 *   (and (= (+ x 1) y) (>= (- y 2) 0))
 *
 * Handles nested lets recursively. Uses parallel binding semantics
 * (all bindings see the original body, not previously substituted ones).
 *
 */
class RewriteLet : public RewriteTermHandler {
public:

    bool canHandle(const std::string& op) const override;

    /**
     * Recursively inline all (let ...) subexpressions in the formula.
     * Returns the rewritten formula without any let bindings.
     */
    std::string rewrite(const std::string& formula) override;

    std::string getName() const override;

private:

    /**
     * Substitute all occurrences of old_var with new_val in expr,
     * respecting word boundaries (space, parentheses, newlines).
     */
    std::string substituteInExpr(
        const std::string& expr,
        const std::string& old_var,
        const std::string& new_val);
};

#endif // REWRITE_LET_H
