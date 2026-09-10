#include <regex>
#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>

#include "parser/smt_parser.h"
#include "nla_handling.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// See definitions further below (near distributeAND) for the doc comments.
static void insertIfMinimal(std::vector<std::vector<LinearInequality>>& polyhedra,
                             std::vector<std::set<std::string>>& keys,
                             std::vector<LinearInequality>&& combined);

DNFFormula SMTParser::parseFormulaToDNF(const std::string& smtFormula,
                                         const std::atomic<bool>* cancel_flag) {
    DNFFormula result;
    std::string trimmed = smtFormula;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    size_t end = trimmed.find_last_not_of(" \t\n\r");

    if (start == std::string::npos) {
        result.polyhedra.push_back({});
        return result;
    }
    trimmed = trimmed.substr(start, end - start + 1);

    if (trimmed == "true") {
        LinearInequality tautology;
        tautology.constant = AffineTerm(0.0);
        tautology.strict = false;
        result.polyhedra.push_back({tautology});
        return result;
    }
    if (trimmed == "false") {
        LinearInequality false_ineq;
        false_ineq.constant = AffineTerm(-1.0);
        false_ineq.strict = false;
        result.polyhedra.push_back({false_ineq});
        return result;
    }

    if (trimmed[0] != '(') {
        std::cerr << "WARNING: unexpected atomic formula: " << trimmed << std::endl;
        result.polyhedra.push_back({});
        return result;
    }

    std::vector<std::string> tokens = splitSExpr(trimmed);
    if (tokens.empty()) {
        result.polyhedra.push_back({});
        return result;
    }

    std::string op = tokens[0];

    // NOT
    if (op == "not") {
        if (tokens.size() != 2) {
            throw std::runtime_error("NOT expects exactly 1 argument");
        }

        DNFFormula inner = parseFormulaToDNF(tokens[1], cancel_flag);

        std::vector<DNFFormula> negated_operands;
        for (const auto& poly : inner.polyhedra) {
            negated_operands.push_back(negateConjunction(poly));
        }

        result = distributeAND(negated_operands, cancel_flag);
        return result;
    }

    // AND
    if (op == "and") {
        std::vector<DNFFormula> operands;
        for (size_t i = 1; i < tokens.size(); ++i) {
            operands.push_back(parseFormulaToDNF(tokens[i], cancel_flag));
        }
        result = distributeAND(operands, cancel_flag);
        return result;
    }

    // OR
    if (op == "or") {
        // Filtered here too, not just inside distributeAND: a redundant OR
        // branch would otherwise carry that bloat into every later AND.
        std::vector<std::set<std::string>> keys;
        for (size_t i = 1; i < tokens.size(); ++i) {
            DNFFormula operand = parseFormulaToDNF(tokens[i], cancel_flag);
            for (auto& poly : operand.polyhedra)
                insertIfMinimal(result.polyhedra, keys, std::move(poly));
        }
        return result;
    }

    // Atomic formulas
    if (op == "=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        std::vector<LinearInequality> constraints = parseAtomicFormula(trimmed);
        result.polyhedra.push_back(constraints);
        return result;
    }

    std::vector<LinearInequality> constraints = parseAtomicFormula(trimmed);
    result.polyhedra.push_back(constraints);
    return result;
}

std::vector<LinearInequality> SMTParser::parseAtomicFormula(const std::string& formula) {
    // After RewriteEquality and ArrayHandler preprocessing, the only
    // atomic formulas reaching here are inequalities: >=, <=, >, <
    std::vector<LinearInequality> result;
    try {
        result.push_back(parseInequality(formula));
    } catch (const NlaTermException& e) {
        if( VERBOSITY == VerbosityLevel::VERBOSE){
            std::cout<< "[SMTParser] Message: " << e.what() << std::endl;
        }
        switch (NLA_HANDLING) {
            case NlaHandling::OVERAPPROXIMATE:
                result.push_back(LinearInequality());              // 0 >= 0 (tautologie)
                break;
            case NlaHandling::UNDERAPPROXIMATE:
                result.push_back(LinearInequality::constructFalse()); // -1 >= 0 (faux)
                break;
            case NlaHandling::EXCEPTION:
                throw;
        }
    }
    return result;
}

DNFFormula SMTParser::negateConjunction(const std::vector<LinearInequality>& conjunction) {
    DNFFormula result;
    
    if (conjunction.empty()) {
        LinearInequality false_ineq;
        false_ineq.constant = AffineTerm(-1.0);
        false_ineq.strict = false;
        result.polyhedra.push_back({false_ineq});
        return result;
    }
    
    for (const auto& ineq : conjunction) {
        LinearInequality negated = ineq;
        
        for (auto& [var, coef] : negated.coefficients) {
            coef.negate();
        }
        negated.constant.negate();
        negated.strict = !negated.strict;
        
        result.polyhedra.push_back({negated});
    }
    
    return result;
}

// Returns true if the polyhedron is trivially unsatisfiable.
// Detects two cases:
//   1. Constant-only contradiction: k >= 0 with k < 0
//   2. Single-variable bound contradiction: v >= lb and v <= ub with lb > ub
//      (only reliable after RewriteStrictInequalities converts strict to non-strict)
static bool isTriviallyUnsat(const std::vector<LinearInequality>& poly) {
    for (const auto& ineq : poly) {
        if (ineq.coefficients.empty() && ineq.constant.constant < 0.0)
            return true;
    }

    std::map<std::string, double> lower_bounds;
    std::map<std::string, double> upper_bounds;

    for (const auto& ineq : poly) {
        if (ineq.coefficients.size() != 1) continue;
        const auto& [var, coef_term] = *ineq.coefficients.begin();
        if (!coef_term.coefficients.empty()) continue;  // parametric coefficient
        double c = coef_term.constant;
        double k = ineq.constant.constant;
        if (std::abs(c) < 1e-12) continue;

        // c*v + k >= 0  =>  v >= -k/c (if c>0)  or  v <= -k/c (if c<0)
        double bound = -k / c;
        if (c > 0) {
            auto it = lower_bounds.find(var);
            lower_bounds[var] = (it == lower_bounds.end()) ? bound : std::max(it->second, bound);
        } else {
            auto it = upper_bounds.find(var);
            upper_bounds[var] = (it == upper_bounds.end()) ? bound : std::min(it->second, bound);
        }
    }

    for (const auto& [var, lb] : lower_bounds) {
        auto it = upper_bounds.find(var);
        if (it != upper_bounds.end() && lb > it->second + 1e-9)
            return true;
    }

    return false;
}

// Cartesian-product DNF expansion: k conjuncts with n1, n2, ... disjuncts
// each produce up to n1*n2*... polyhedra. isTriviallyUnsat() prunes some
// (single-variable numeric bound contradictions only), but nothing bounds
// the blowup itself -- a handful of source-level `or`s combined with
// ArrayHandler's own index-equality case-splits can reach thousands of
// polyhedra on a genuinely small instance (confirmed: a 3-variable loop
// body -> 48, a 40-variable one -> 2592), and this runs entirely before any
// SMT solver is ever invoked, so SMTSolverInterface::interrupt() can't reach
// it. Poll cancel_flag (threaded down from the calling technique's own
// cancellation flag -- see parseFormulaToDNF's doc comment) periodically and
// bail out with a distinguishable exception instead of running forever once
// that technique has been told to stop.
static void checkCancellation(const std::atomic<bool>* cancel_flag) {
    if (cancel_flag && cancel_flag->load(std::memory_order_relaxed)) {
        throw PreprocessingCancelledException(
            "DNF expansion cancelled while still combining disjuncts -- this "
            "instance's boolean/array case-split structure likely blows up "
            "combinatorially before any SMT solving even starts");
    }
}

// Order-independent key for a conjunct: the set of its constraints' SMT-LIB2
// text. Two conjuncts with the same constraints in any order get the same
// key, for insertIfMinimal()'s duplicate/subsumption detection below.
static std::set<std::string> conjunctKey(const std::vector<LinearInequality>& poly) {
    std::set<std::string> key;
    for (const auto& ineq : poly) key.insert(ineq.toSMTLib2());
    return key;
}

// Inserts `combined` unless it's redundant within the disjunction: if an
// already-kept polyhedron's constraints are a SUBSET of `combined`'s, that
// one is weaker/more general and already implies it ("A OR B" with
// constraints(A) subset-of constraints(B) simplifies to just "A"). Evicts the
// reverse case symmetrically. Mirrors Ultimate XnfTransformer's
// XJunctionPosetMinimalElements. Fused into construction (per Cartesian-
// product combination, not a post-pass) so a redundant conjunct never
// compounds through the NEXT operand's round -- that cross-operand
// compounding is what turns a handful of `or`s into thousands of polyhedra.
static void insertIfMinimal(std::vector<std::vector<LinearInequality>>& polyhedra,
                             std::vector<std::set<std::string>>& keys,
                             std::vector<LinearInequality>&& combined) {
    std::set<std::string> new_key = conjunctKey(combined);
    for (const auto& existing_key : keys) {
        if (std::includes(new_key.begin(), new_key.end(),
                           existing_key.begin(), existing_key.end())) {
            return;  // dominated by an already-kept, more general polyhedron
        }
    }
    size_t write = 0;
    for (size_t read = 0; read < keys.size(); ++read) {
        bool dominated_by_new = std::includes(keys[read].begin(), keys[read].end(),
                                               new_key.begin(), new_key.end());
        if (dominated_by_new) continue;  // evict: `combined` is more general
        if (read != write) {
            polyhedra[write] = std::move(polyhedra[read]);
            keys[write] = std::move(keys[read]);
        }
        ++write;
    }
    polyhedra.resize(write);
    keys.resize(write);
    polyhedra.push_back(std::move(combined));
    keys.push_back(std::move(new_key));
}

DNFFormula SMTParser::distributeAND(const std::vector<DNFFormula>& operands,
                                     const std::atomic<bool>* cancel_flag) {
    if (operands.empty()) {
        DNFFormula result;
        result.polyhedra.push_back({});
        return result;
    }

    if (operands.size() == 1) return operands[0];

    DNFFormula result = operands[0];

    for (size_t i = 1; i < operands.size(); ++i) {
        checkCancellation(cancel_flag);
        DNFFormula new_result;
        std::vector<std::set<std::string>> new_keys;

        size_t combos_since_check = 0;
        for (const auto& poly1 : result.polyhedra) {
            for (const auto& poly2 : operands[i].polyhedra) {
                // Also checked inside the innermost loop, throttled: a
                // single (i.e. i fixed) combination step can itself be
                // huge if both sides already grew large from earlier
                // conjuncts, well before the next outer-loop checkpoint.
                // An atomic load is far cheaper than the wall-clock read
                // this replaced, so this can afford to check much more often.
                if (++combos_since_check >= 4096) {
                    combos_since_check = 0;
                    checkCancellation(cancel_flag);
                }
                std::vector<LinearInequality> combined = poly1;
                combined.insert(combined.end(), poly2.begin(), poly2.end());
                if (!isTriviallyUnsat(combined))
                    insertIfMinimal(new_result.polyhedra, new_keys, std::move(combined));
            }
        }

        result = new_result;
    }

    return result;
}

LinearInequality SMTParser::parseInequality(const std::string& expr) {
    LinearInequality ineq;
    ineq.strict = false;

    auto tokens = splitSExpr(expr);
    if (tokens.size() != 3) {
        throw std::runtime_error(
            "SMTParser::parseInequality: expected (op lhs rhs), got: " + expr);
    }

    const std::string& op = tokens[0];

    // Validation: operators that should have been rewritten before reaching here
    if (op == "=" || op == "div" || op == "mod" || op == "store") {
        throw std::runtime_error(
            "SMTParser::parseInequality: unrewritten operator '" + op +
            "' in: " + expr +
            "\nEnsure RewriteEquality / RewriteDivisionMod / ArrayHandler are applied first.");
    }

    AffineTerm lhs = parseArithExpr(tokens[1]);
    AffineTerm rhs = parseArithExpr(tokens[2]);
    AffineTerm diff;

    if (op == ">=" || op == ">") {
        diff = lhs - rhs;
    } else if (op == "<" || op == "<=") {
        diff = rhs - lhs;
    } else {
        throw std::runtime_error(
            "SMTParser::parseInequality: unsupported operator '" + op + "' in: " + expr);
    }

    for (const auto& [var, coef] : diff.coefficients) {
        ineq.coefficients[var] = AffineTerm(coef);
    }
    ineq.constant = AffineTerm(diff.constant);
    ineq.strict = (op == ">" || op == "<");

    return ineq;
}

AffineTerm SMTParser::parseArithExpr(const std::string& expr) {
    std::string cleaned = expr;
    cleaned.erase(0, cleaned.find_first_not_of(" \t\n\r"));
    cleaned.erase(cleaned.find_last_not_of(" \t\n\r") + 1);
    
    if (std::regex_match(cleaned, std::regex("-?[0-9]+(\\.[0-9]+)?"))) {
        return AffineTerm(std::stod(cleaned));
    }
    
    // SMT-LIB2 quoted identifier |...| — treat as an atomic variable regardless
    // of any parentheses or special characters inside the pipes.
    if (cleaned.size() >= 2 && cleaned.front() == '|' && cleaned.back() == '|') {
        AffineTerm result;
        result.coefficients[cleaned] = 1.0;
        return result;
    }

    if (cleaned.find('(') == std::string::npos) {
        AffineTerm result;
        result.coefficients[cleaned] = 1.0;
        return result;
    }
    
    if (cleaned.find("(select") == 0) {
        auto parts = splitSExpr(cleaned);
        if (parts.size() == 3 && parts[0] == "select") {
            std::string symbolic_var = parts[1] + "[" + parts[2] + "]";
            AffineTerm result;
            result.coefficients[symbolic_var] = 1.0;
            return result;
        }
    }
    
    if (cleaned.find("(*") == 0) {
        auto parts = splitSExpr(cleaned);
        if (parts.size() == 3 && parts[0] == "*") {
            // (* a b) — one operand must be a constant (possibly expressed as an SMT
            // sub-expression like (- 1)), the other a linear expression.
            // Evaluate both sides; if one is variable-free, it is the scalar.
            AffineTerm t1 = parseArithExpr(parts[1]);
            AffineTerm t2 = parseArithExpr(parts[2]);
            if (t1.coefficients.empty()) {
                t2 *= t1.constant;
                return t2;
            } else if (t2.coefficients.empty()) {
                t1 *= t2.constant;
                return t1;
            } else {
                throw NlaTermException(
                    "SMTParser::parseArithExpr: non-linear multiplication: " + cleaned);
            }
        } else {
            // (* a b c ...) with >2 operands — always non-linear
            throw NlaTermException(
                "SMTParser::parseArithExpr: non-linear multiplication: " + cleaned);
        }
    }
    
    if (cleaned.find("(+") == 0) {
        auto parts = splitSExpr(cleaned);
        AffineTerm result;
        for (size_t i = 1; i < parts.size(); ++i) {
            if (parts[i] == "+") continue;
            result += parseArithExpr(parts[i]);
        }
        return result;
    }
    
    if (cleaned.find("(-") == 0) {
        auto parts = splitSExpr(cleaned);
        if (parts.size() == 2) {
            AffineTerm result = parseArithExpr(parts[1]);
            result.negate();
            return result;
        } else if (parts.size() == 3) {
            return parseArithExpr(parts[1]) - parseArithExpr(parts[2]);
        }
    }
    
    throw std::runtime_error(
        "SMTParser::parseArithExpr: unsupported expression: " + cleaned +
        "\nIf this is a function call, ensure FormulaLinearizer is applied before parsing.");
}

// splitSExpr() is now inline in the header, delegating to SExprUtils.

void SMTParser::extractVariables(const std::string& smtFormula, std::vector<std::string>& vars) {
    std::regex var_regex("v_[a-zA-Z0-9_~]+");
    std::smatch match;
    
    std::string::const_iterator searchStart(smtFormula.cbegin());
    while (std::regex_search(searchStart, smtFormula.cend(), match, var_regex)) {
        std::string var_name = match[0].str();
        
        if (std::find(vars.begin(), vars.end(), var_name) == vars.end()) {
            vars.push_back(var_name);
        }
        
        searchStart = match.suffix().first;
    }
}

bool SMTParser::isArithmeticOp(const std::string& op) {
    return op == "+" || op == "-" || op == "*" || op == "/";
}