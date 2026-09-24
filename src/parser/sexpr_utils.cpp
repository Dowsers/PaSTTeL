#include "parser/sexpr_utils.h"
#include <stack>
#include <cctype>

namespace SExprUtils {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

std::vector<std::string> splitSExpr(const std::string& expr) {
    std::vector<std::string> result;
    std::string cleaned = trim(expr);
    if (cleaned.empty()) return result;

    // Remove enclosing parentheses
    if (cleaned.front() == '(' && cleaned.back() == ')') {
        cleaned = cleaned.substr(1, cleaned.size() - 2);
    }

    std::stack<int> parens;
    std::string current;

    for (size_t i = 0; i < cleaned.size(); ++i) {
        char c = cleaned[i];
        if (c == '|') {
            // SMT-LIB2 quoted identifier: |...| — consume until closing pipe,
            // treating all internal characters (including parentheses) as part of the token.
            current += c;
            ++i;
            while (i < cleaned.size() && cleaned[i] != '|') {
                current += cleaned[i];
                ++i;
            }
            if (i < cleaned.size()) {
                current += cleaned[i]; // closing '|'
            }
        } else if (c == '(') {
            parens.push(i);
            current += c;
        } else if (c == ')') {
            current += c;
            if (!parens.empty()) parens.pop();
        } else if (std::isspace(c) && parens.empty()) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        result.push_back(current);
    }
    return result;
}

std::vector<std::string> collectSymbols(const std::string& expr) {
    std::vector<std::string> symbols;
    const size_t n = expr.size();
    std::string current;

    auto flush = [&]() {
        if (!current.empty()) {
            symbols.push_back(current);
            current.clear();
        }
    };

    for (size_t i = 0; i < n; ++i) {
        char c = expr[i];
        if (c == '|') {
            // SMT-LIB2 quoted symbol: everything up to the next '|' is a single
            // atom, regardless of internal spaces / parens / commas / brackets.
            flush();
            std::string quoted(1, '|');
            ++i;
            while (i < n && expr[i] != '|') {
                quoted += expr[i];
                ++i;
            }
            if (i < n) quoted += '|';   // closing pipe (kept)
            symbols.push_back(quoted);
        } else if (c == '(' || c == ')' ||
                   std::isspace(static_cast<unsigned char>(c))) {
            // Delimiters for unquoted atoms.
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return symbols;
}

bool isNumericLiteral(const std::string& s) {
    std::string trimmed = trim(s);
    if (trimmed.empty()) return false;

    size_t i = 0;
    if (trimmed[i] == '-') {
        ++i;
        if (i >= trimmed.size()) return false;
    }

    bool has_digits = false;
    while (i < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
        has_digits = true;
        ++i;
    }
    if (!has_digits) return false;

    // Optional decimal part
    if (i < trimmed.size() && trimmed[i] == '.') {
        ++i;
        bool has_decimal_digits = false;
        while (i < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            has_decimal_digits = true;
            ++i;
        }
        if (!has_decimal_digits) return false;
    }

    return i == trimmed.size();
}

void collectLeafAtoms(const std::string& expr, std::set<std::string>& out) {
    std::string trimmed = trim(expr);
    if (trimmed.empty()) return;

    if (trimmed.front() == '(') {
        auto tokens = splitSExpr(trimmed);
        // tokens[0] is the form's head (operator/function symbol) -- never a
        // variable reference, regardless of what it is.
        for (size_t i = 1; i < tokens.size(); ++i)
            collectLeafAtoms(tokens[i], out);
        return;
    }

    if (isNumericLiteral(trimmed) || trimmed == "true" || trimmed == "false")
        return;

    out.insert(trimmed);
}

} // namespace SExprUtils
