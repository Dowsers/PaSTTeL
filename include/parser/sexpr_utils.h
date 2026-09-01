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
 * Collect every atomic symbol (leaf token) occurring in an S-expression.
 *
 * This is the pipe-aware counterpart to a naive whitespace split: an SMT-LIB2
 * quoted symbol |...| is returned as ONE atom (WITH its surrounding pipes),
 * even when its content contains spaces, commas, brackets or parentheses —
 * e.g. |v_arrayCell_v_#memory_int_1[base_2, (+ off_3 4)]_1|. Parentheses and
 * whitespace are the only delimiters for unquoted atoms.
 *
 * Returned atoms are the raw tokens as they appear (quoted symbols keep their
 * pipes); callers that compare against pipe-stripped identifiers should strip
 * the pipes themselves. Numeric literals and operators are returned too; they
 * are harmless for membership tests.
 *
 * Examples:
 *   "(and a |b c| 12)"          -> ["and", "a", "|b c|", "12"]
 *   "(<= v_x_1 |arr[i, j]_1|)"  -> ["<=", "v_x_1", "|arr[i, j]_1|"]
 */
std::vector<std::string> collectSymbols(const std::string& expr);

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
