#include <sstream>

#include "rewriting/rewrite_strict_inequalities.h"
#include "parser/sexpr_utils.h"

bool RewriteStrictInequalities::canHandle(const std::string& op) const {
    return op == "<" || op == ">";
}

std::string RewriteStrictInequalities::getName() const {
    return "RewriteStrictInequalities";
}

std::string RewriteStrictInequalities::rewrite(const std::string& formula) {
    std::string trimmed = SExprUtils::trim(formula);

    if (trimmed.empty() || trimmed[0] != '(') return trimmed;

    auto tokens = SExprUtils::splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    const std::string& op = tokens[0];

    // (< a b)  -->  (<= a (- b 1))
    if (op == "<" && tokens.size() == 3) {
        std::string a = rewrite(tokens[1]);
        std::string b = rewrite(tokens[2]);
        return "(<= " + a + " (- " + b + " 1))";
    }

    // (> a b)  -->  (>= a (+ b 1))
    if (op == ">" && tokens.size() == 3) {
        std::string a = rewrite(tokens[1]);
        std::string b = rewrite(tokens[2]);
        return "(>= " + a + " (+ " + b + " 1))";
    }

    // Recurse on children
    std::ostringstream out;
    out << "(" << op;
    for (size_t i = 1; i < tokens.size(); ++i)
        out << " " << rewrite(tokens[i]);
    out << ")";
    return out.str();
}
