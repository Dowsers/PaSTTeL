#ifndef SEXPR_UTILS_H
#define SEXPR_UTILS_H

#include <string>
#include <vector>

/**
 * Shared S-expression utilities.
 *
 * Provides common operations on SMT-LIB2 S-expressions used across
 * the parser, linearizer, and rewriting modules.
 */
namespace SExprUtils {

/**
 * Split an S-expression into its top-level tokens.
 *
 * If the expression is parenthesized, the outer parentheses are stripped
 * and the operator plus arguments are returned as separate tokens.
 *
 * Examples:
 *   "(+ (f x) y)"  ->  ["+", "(f x)", "y"]
 *   "(and a b c)"  ->  ["and", "a", "b", "c"]
 *   "atom"         ->  ["atom"]
 */
std::vector<std::string> splitSExpr(const std::string& expr);

/**
 * Trim leading and trailing whitespace from a string.
 */
std::string trim(const std::string& s);

/**
 * Check if a string is a numeric literal (integer or decimal, possibly negative).
 * Matches: "123", "-45", "3.14", "-2.5"
 * Does NOT match: "x", "(+ x 1)", ""
 */
bool isNumericLiteral(const std::string& s);

} // namespace SExprUtils

#endif // SEXPR_UTILS_H
