#include <sstream>
#include <iostream>
#include <cctype>

#include "rewriting/rewrite_division_modulo.h"
#include "utiles.h"

// ============================================================================
// CONSTRUCTOR / RESET
// ============================================================================

RewriteDivisionMod::RewriteDivisionMod()
    : m_counter(0)
{
}

bool RewriteDivisionMod::canHandle(const std::string& op) const {
    return op == "div" || op == "mod" || op == "/";
}

std::string RewriteDivisionMod::getName() const {
    return "RewriteDivisionMod";
}

void RewriteDivisionMod::reset() {
    m_counter = 0;
    m_aux_vars.clear();
    m_aux_constraints.clear();
    m_div_cache.clear();
    m_mod_cache.clear();
}

// ============================================================================
// PUBLIC API
// ============================================================================

std::string RewriteDivisionMod::rewrite(const std::string& formula) {
    if (formula.empty() || formula == "true") {
        return formula;
    }

    // Check if formula contains div or mod at all (fast path)
    if (formula.find("div") == std::string::npos &&
        formula.find("mod") == std::string::npos &&
        formula.find("/") == std::string::npos) {
        return formula;
    }

    m_aux_constraints.clear();

    // Recursively rewrite the formula
    std::string rewritten = rewriteExpr(formula);

    // If no constraints were generated, formula is unchanged
    if (m_aux_constraints.empty()) {
        return rewritten;
    }

    // Conjoin auxiliary constraints to the rewritten formula
    std::ostringstream result;
    result << "(and " << rewritten;
    for (const auto& constraint : m_aux_constraints) {
        result << " " << constraint;
    }
    result << ")";

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  [RewriteDivisionMod] Replaced " << m_aux_constraints.size()
                  << " div/mod operation(s) with linear constraints" << std::endl;
    }

    return result.str();
}

std::vector<FunctionAbstraction> RewriteDivisionMod::getAuxVars() const {
    return m_aux_vars;
}

// ============================================================================
// RECURSIVE REWRITING
// ============================================================================

std::string RewriteDivisionMod::rewriteExpr(const std::string& expr) {
    std::string trimmed = expr;
    {
        size_t s = trimmed.find_first_not_of(" \t\n\r");
        size_t e = trimmed.find_last_not_of(" \t\n\r");
        if (s == std::string::npos) return expr;
        trimmed = trimmed.substr(s, e - s + 1);
    }

    // Atom: return as-is
    if (trimmed[0] != '(') {
        return trimmed;
    }

    // S-expression: split into tokens
    std::vector<std::string> tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    std::string op = tokens[0];

    // Recursively rewrite all arguments first
    std::vector<std::string> rewritten_args;
    for (size_t i = 1; i < tokens.size(); ++i) {
        rewritten_args.push_back(rewriteExpr(tokens[i]));
    }

    // Handle (div dividend divisor) — integer division
    if (op == "div" && rewritten_args.size() == 2) {
        BigInt a, b;
        // Both operands are integer constants → fold at compile time
        if (isIntLiteral(rewritten_args[0], a) && isIntLiteral(rewritten_args[1], b)) {
            if (b == 0) return "0"; // undefined; leave as 0 (or emit an error)
            // SMT-LIB truncates toward zero for `div`
            return toStringBigInt(a / b);
        }
        return getOrCreateDivVar(rewritten_args[0], rewritten_args[1], false);
    }

    // Handle (/ dividend divisor) — Real division
    if (op == "/" && rewritten_args.size() == 2) {
        double a, b;
        // Both operands are numeric constants → fold
        if (isNumericLiteral(rewritten_args[0], a) && isNumericLiteral(rewritten_args[1], b)) {
            if (b == 0.0) return "0.0";
            double result = a / b;
            // Emit as a decimal literal recognisable by SMT-LIB
            std::ostringstream oss;
            oss << std::fixed << result;   // keeps enough precision for typical use
            return oss.str();
        }
        return getOrCreateDivVar(rewritten_args[0], rewritten_args[1], true);
    }

    // Handle (mod dividend divisor) — integer modulo
    if (op == "mod" && rewritten_args.size() == 2) {
        BigInt a, b;
        // Both operands are integer constants → fold
        if (isIntLiteral(rewritten_args[0], a) && isIntLiteral(rewritten_args[1], b)) {
            if (b == 0) return "0";
            // SMT-LIB mod is always non-negative
            BigInt r = a % b;
            if (r < 0) r += absBigInt(b);
            return toStringBigInt(r);
        }
        return getOrCreateModVar(rewritten_args[0], rewritten_args[1]);
    }


    // Rebuild expression with rewritten arguments
    std::ostringstream rebuilt;
    rebuilt << "(" << op;
    for (const auto& arg : rewritten_args) {
        rebuilt << " " << arg;
    }
    rebuilt << ")";
    return rebuilt.str();
}

// ============================================================================
// DIV REWRITING
// ============================================================================

std::string RewriteDivisionMod::getOrCreateDivVar(
    const std::string& dividend, const std::string& divisor, bool isReal) {

    // Check cache
    std::string key = (isReal ? "(/ " : "(div ") + dividend + " " + divisor + ")";
    auto it = m_div_cache.find(key);
    if (it != m_div_cache.end()) {
        return it->second;
    }

    // Create fresh quotient variable
    std::string q = createFreshVar("div_aux");

    // Register aux var for solver declaration
    FunctionAbstraction abs;
    abs.fresh_var = q;
    abs.original_call = "";  // no simple equality assertion
    abs.function_name = isReal ? "/" : "div";
    abs.sort          = isReal ? "Real" : "Int";
    m_aux_vars.push_back(abs);

    std::ostringstream constraint;

    if (isReal) {
        // Real division: q * divisor = dividend  (exact, no remainder)
        constraint << "(= " << dividend << " (* " << q << " " << divisor << "))";
    } else {
        // Integer division (SMT-LIB semantics, truncation toward zero):
        //
        //   divisor > 0  →  q*divisor ≤ dividend < (q+1)*divisor
        //   divisor < 0  →  q*divisor ≤ dividend < (q-1)*divisor
        constraint << "(or "
                    << "(and (>= " << divisor << " 1) "
                    <<       "(<= (* " << divisor << " " << q << ") " << dividend << ") "
                    <<       "(<= " << dividend << " (- (* " << divisor << " (+ " << q << " 1)) 1))) "
                    << "(and (<= " << divisor << " (- 0 1)) "
                    <<       "(<= (* " << divisor << " " << q << ") " << dividend << ") "
                    <<       "(<= " << dividend << " (- (* " << divisor << " (- " << q << " 1)) 1))))";
    }
    m_aux_constraints.push_back(constraint.str());

    // Cache and return
    m_div_cache[key] = q;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "    [RewriteDivisionMod] " << key << " -> " << q << std::endl;
    }

    return q;
}

// ============================================================================
// MOD REWRITING
// ============================================================================

std::string RewriteDivisionMod::getOrCreateModVar(
    const std::string& dividend, const std::string& divisor) {

    // Check cache
    std::string key = "(mod " + dividend + " " + divisor + ")";
    auto it = m_mod_cache.find(key);
    if (it != m_mod_cache.end()) {
        return it->second;
    }

    // Get or create the quotient variable (shared with div if same operands)
    std::string q = getOrCreateDivVar(dividend, divisor, false);

    // Create fresh remainder variable
    std::string r = createFreshVar("mod_aux");

    // Register aux var for solver declaration
    FunctionAbstraction abs;
    abs.fresh_var = r;
    abs.original_call = "";  // no simple equality assertion
    abs.function_name = "mod";
    abs.sort = "Int";
    m_aux_vars.push_back(abs);

    // Constraints:
    //   divisor > 0  →  0 ≤ r ≤ divisor-1  ∧  dividend = q*divisor + r
    //   divisor < 0  →  0 ≤ r ≤ -divisor-1 ∧  dividend = q*divisor + r
    std::ostringstream constraint;
    constraint << "(or "
                << "(and (>= " << divisor << " 1) "
                <<       "(<= 0 " << r << ") "
                <<       "(<= " << r << " (- " << divisor << " 1)) "
                <<       "(= " << dividend << " (+ (* " << divisor << " " << q << ") " << r << "))) "
                << "(and (<= " << divisor << " (- 0 1)) "
                <<       "(<= 0 " << r << ") "
                <<       "(<= " << r << " (- (- 0 " << divisor << ") 1)) "
                <<       "(= " << dividend << " (+ (* " << divisor << " " << q << ") " << r << "))))";

    m_aux_constraints.push_back(constraint.str());

    // Cache and return
    m_mod_cache[key] = r;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "    [RewriteDivisionMod] " << key << " -> " << r
                  << " (quotient: " << q << ")" << std::endl;
    }

    return r;
}

// ============================================================================
// UTILITIES
// ============================================================================

std::string RewriteDivisionMod::createFreshVar(const std::string& prefix) {
    return prefix + "_" + std::to_string(m_counter++);
}

bool RewriteDivisionMod::isIntLiteral(const std::string& s, BigInt& value) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-') {
        if (s.size() == 1) return false;
        start = 1;
    }
    for (size_t i = start; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    try {
        value = std::stoll(s);
        return true;
    } catch (...) {
        return false;
    }
}

// Returns true iff `s` is an integer or decimal literal (e.g. "3", "3.14", "-2.0")
bool RewriteDivisionMod::isNumericLiteral(const std::string& s, double& value) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-') {
        if (s.size() == 1) return false;
        start = 1;
    }
    bool hasDot = false;
    for (size_t i = start; i < s.size(); ++i) {
        if (s[i] == '.') {
            if (hasDot) return false; // two dots
            hasDot = true;
        } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    try {
        value = std::stod(s);
        return true;
    } catch (...) {
        return false;
    }
}

bool RewriteDivisionMod::isPositiveIntLiteral(const std::string& s, BigInt& value) {
    return isIntLiteral(s, value) && value > 0;
}

// splitSExpr() is now inline in the header, delegating to SExprUtils.
