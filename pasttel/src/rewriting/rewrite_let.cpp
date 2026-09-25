#include <sstream>
#include <vector>

#include "rewriting/rewrite_let.h"
#include "parser/sexpr_utils.h"


bool RewriteLet::canHandle(const std::string& op) const {
    return op == "let";
}
std::string RewriteLet::getName() const {
    return "RewriteLet";
}

std::string RewriteLet::rewrite(const std::string& formula) {
    std::string trimmed = SExprUtils::trim(formula);
    if (trimmed.empty() || trimmed == "true") return formula;

    // Fast path: no let in formula
    if (trimmed.find("let") == std::string::npos) return trimmed;

    // Atom — return as-is
    if (trimmed.empty() || trimmed[0] != '(') return trimmed;

    auto tokens = SExprUtils::splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    const std::string& op = tokens[0];

    // (let ((x1 t1) (x2 t2) ...) body)
    if (op == "let" && tokens.size() == 3) {
        std::string bindings_str = tokens[1];
        std::string body = rewrite(tokens[2]);

        // Parse bindings: each is "(var term)"
        auto binding_list = SExprUtils::splitSExpr(bindings_str);
        std::vector<std::pair<std::string, std::string>> substitutions;
        for (const auto& binding : binding_list) {
            auto parts = SExprUtils::splitSExpr(binding);
            if (parts.size() == 2) {
                substitutions.push_back({parts[0], rewrite(parts[1])});
            }
        }

        // Apply substitutions (parallel semantics: all use original body)
        for (const auto& [var, term] : substitutions) {
            body = substituteInExpr(body, var, term);
        }

        return body;
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

std::string RewriteLet::substituteInExpr(
    const std::string& expr, const std::string& old_var, const std::string& new_val)
{
    std::string result = expr;
    size_t pos = 0;
    while ((pos = result.find(old_var, pos)) != std::string::npos) {
        bool start_ok = (pos == 0) ||
            result[pos - 1] == ' ' || result[pos - 1] == '(' || result[pos - 1] == '\n';
        size_t end_pos = pos + old_var.size();
        bool end_ok = (end_pos == result.size()) ||
            result[end_pos] == ' ' || result[end_pos] == ')' || result[end_pos] == '\n';

        if (start_ok && end_ok) {
            result.replace(pos, old_var.size(), new_val);
            pos += new_val.size();
        } else {
            pos += old_var.size();
        }
    }
    return result;
}