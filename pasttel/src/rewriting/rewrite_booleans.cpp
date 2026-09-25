#include <sstream>

#include "rewriting/rewrite_booleans.h"
#include "parser/sexpr_utils.h"
#include <iostream>

RewriteBooleans::RewriteBooleans(const std::set<std::string>& bool_ssa_vars)
    : m_bool_vars(bool_ssa_vars) {}

bool RewriteBooleans::isBoolVar(const std::string& token) const {
    for(const auto& s: m_bool_vars) {
        if(token.find(s) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool RewriteBooleans::canHandle(const std::string& op) const {
    return isBoolVar(op);  //|| op == "not"  || op == "and" || op == "or" || op == "=>" || op == "xor";
}

std::string RewriteBooleans::getName() const {
    return "RewriteBooleans";
}

std::string RewriteBooleans::rewrite(const std::string& formula) {
    if (m_bool_vars.empty()) return formula;
    
    std::string trimmed = SExprUtils::trim(formula);

    if (trimmed.empty() || trimmed == "true") return formula;

    std::string rewritten = rewriteExpr(trimmed);

    // Inject 0/1 bounds for each Bool SSA var.
    // std::string bounds;
    // for (const auto& bv : m_bool_vars) {
    //     std::string bound = "(and (>= " + bv + " 0) (<= " + bv + " 1))";
    //     bounds = bounds.empty() ? bound : "(and " + bounds + " " + bound + ")";
    // }

    if (rewritten == "true" || rewritten.empty()) return rewritten;
    return rewritten;
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

    // (<bool_var>)  -->  (>= <bool_var> 1)
    if(isBoolVar(op) && tokens.size() == 1) {
        return "(>= " + op + " 1)";
    }
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
