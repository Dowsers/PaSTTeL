#ifndef REWRITE_LET_H
#define REWRITE_LET_H

#include <string>

/**
 * RewriteLet - Inline (expand) let bindings in SMT-LIB2 formulas.
 *
 * Inspired by Ultimate's FormulaUnLet:
 *   (let ((.cse0 (+ x 1)) (.cse1 (- y 2)))
 *     (and (= .cse0 y) (>= .cse1 0)))
 *   -->
 *   (and (= (+ x 1) y) (>= (- y 2) 0))
 *
 * Handles nested lets recursively. Uses parallel binding semantics
 * (all bindings see the original body, not previously substituted ones).
 *
 * Stateless: all methods are static.
 */
class RewriteLet {
public:
    /**
     * Recursively inline all (let ...) subexpressions in the formula.
     * Returns the rewritten formula without any let bindings.
     */
    static std::string rewrite(const std::string& formula);

private:
    /**
     * Recursively process an S-expression, expanding let bindings.
     */
    static std::string rewriteExpr(const std::string& expr);

    /**
     * Substitute all occurrences of old_var with new_val in expr,
     * respecting word boundaries (space, parentheses, newlines).
     */
    static std::string substituteInExpr(
        const std::string& expr,
        const std::string& old_var,
        const std::string& new_val);
};

#endif // REWRITE_LET_H
