#include <sstream>

#include "rewriting/rewrite_booleans.h"
#include "parser/sexpr_utils.h"

RewriteBooleans::RewriteBooleans(const std::set<std::string>& bool_ssa_vars)
    : m_bool_vars(bool_ssa_vars) {}

bool RewriteBooleans::isBoolVar(const std::string& token) const {
    return m_bool_vars.count(token) > 0;
}

std::string RewriteBooleans::rewrite(const std::string& formula) const {
    if (m_bool_vars.empty()) return formula;
    std::string trimmed = SExprUtils::trim(formula);
    if (trimmed.empty() || trimmed == "true") return formula;
    return rewriteExpr(trimmed);
}

std::string RewriteBooleans::rewriteWithBounds(const std::string& formula) const {
    if (m_bool_vars.empty()) return formula;

    std::string rewritten = rewrite(formula);

    // Inject 0/1 bounds for each Bool SSA var.
    // This linearizes ite(b, 1, 0), matching Ultimate's replacement semantics.
    std::string bounds;
    for (const auto& bv : m_bool_vars) {
        std::string bound = "(and (>= " + bv + " 0) (<= " + bv + " 1))";
        bounds = bounds.empty() ? bound : "(and " + bounds + " " + bound + ")";
    }

    if (bounds.empty()) return rewritten;
    if (rewritten == "true" || rewritten.empty()) return bounds;
    return "(and " + rewritten + " " + bounds + ")";
}

std::string RewriteBooleans::rewriteExpr(const std::string& expr) const {
    std::string trimmed = SExprUtils::trim(expr);

    // Bare boolean variable: v  -->  (>= v 1)
    if (trimmed[0] != '(' && isBoolVar(trimmed)) {
        return "(>= " + trimmed + " 1)";
    }

    // Not an S-expression — return as-is
    if (trimmed.empty() || trimmed[0] != '(') return trimmed;

    auto tokens = SExprUtils::splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    const std::string& op = tokens[0];

    // (not <bool_var>)  -->  (<= <bool_var> 0)
    if (op == "not" && tokens.size() == 2) {
        std::string inner = SExprUtils::trim(tokens[1]);
        if (inner[0] != '(' && isBoolVar(inner)) {
            return "(<= " + inner + " 0)";
        }
        // Otherwise recurse normally on the inner expression
        std::string rewritten_inner = rewriteExpr(tokens[1]);
        return "(not " + rewritten_inner + ")";
    }

    // For and/or: recurse on children, which may be bare boolean vars
    // For comparisons: operands are arithmetic, no booleans expected — still recurse
    std::ostringstream out;
    out << "(" << op;
    for (size_t i = 1; i < tokens.size(); ++i) {
        out << " " << rewriteExpr(tokens[i]);
    }
    out << ")";
    return out.str();
}
