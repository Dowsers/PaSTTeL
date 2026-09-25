#include <sstream>

#include "rewriting/rewrite_equality.h"
#include "parser/sexpr_utils.h"

bool RewriteEquality::canHandle(const std::string& op) const {
    return op == "=" || op == "not";
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

    if (integer_mode_ && op == "not" && tokens.size() == 2) {
        auto inner = SExprUtils::splitSExpr(SExprUtils::trim(tokens[1]));
        if (inner.size() == 3 && inner[0] == "=") {
            std::string a = rewrite(inner[1]);
            std::string b = rewrite(inner[2]);
            return "(or (>= " + a + " (+ " + b + " 1)) (<= " + a + " (- " + b + " 1)))";
        }
    }

    // (= a b) --> (and (<= a b) (>= a b))
    if (op == "=" && tokens.size() == 3) {
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
