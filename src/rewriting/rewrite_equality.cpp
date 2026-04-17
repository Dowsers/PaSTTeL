#include <sstream>

#include "rewriting/rewrite_equality.h"
#include "parser/sexpr_utils.h"

bool RewriteEquality::canHandle(const std::string& op) const {
    return op == "=";
}

std::string RewriteEquality::getName() const {
        return "RewriteEquality";
}

std::string RewriteEquality::rewrite(const std::string& formula) {
    std::string trimmed = SExprUtils::trim(formula);

    // Not an S-expression — return as-is
    if (trimmed.empty() || trimmed[0] != '(' || trimmed == "true") return trimmed;

    auto tokens = SExprUtils::splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    const std::string& op = tokens[0];

    // (= a b) --> (and (<= a b) (>= a b))
    if (op == "=" && tokens.size() == 3) {
        // Recursively rewrite the operands first
        std::string lhs = rewrite(tokens[1]);
        std::string rhs = rewrite(tokens[2]);
        return "(and (<= " + lhs + " " + rhs + ") (>= " + lhs + " " + rhs + "))";
    }

    // For any other compound expression, recurse on children
    std::ostringstream out;
    out << "(" << op;
    for (size_t i = 1; i < tokens.size(); ++i) {
        out << " " << rewrite(tokens[i]);
    }
    out << ")";
    return out.str();
}
