#include <regex>
#include <iostream>
#include <stdexcept>

#include "parser/smt_parser.h"
#include "nla_handling.h"


DNFFormula SMTParser::parseFormulaToDNF(const std::string& smtFormula) {
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
        
        DNFFormula inner = parseFormulaToDNF(tokens[1]);
        
        std::vector<DNFFormula> negated_operands;
        for (const auto& poly : inner.polyhedra) {
            negated_operands.push_back(negateConjunction(poly));
        }
        
        result = distributeAND(negated_operands);
        return result;
    }
    
    // AND
    if (op == "and") {
        std::vector<DNFFormula> operands;
        for (size_t i = 1; i < tokens.size(); ++i) {
            operands.push_back(parseFormulaToDNF(tokens[i]));
        }
        result = distributeAND(operands);
        return result;
    }
    
    // OR
    if (op == "or") {
        for (size_t i = 1; i < tokens.size(); ++i) {
            DNFFormula operand = parseFormulaToDNF(tokens[i]);
            result.polyhedra.insert(result.polyhedra.end(),
                                    operand.polyhedra.begin(),
                                    operand.polyhedra.end());
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

DNFFormula SMTParser::distributeAND(const std::vector<DNFFormula>& operands) {
    if (operands.empty()) {
        DNFFormula result;
        result.polyhedra.push_back({});
        return result;
    }
    
    if (operands.size() == 1) return operands[0];
    
    DNFFormula result = operands[0];
    
    for (size_t i = 1; i < operands.size(); ++i) {
        DNFFormula new_result;
        
        for (const auto& poly1 : result.polyhedra) {
            for (const auto& poly2 : operands[i].polyhedra) {
                std::vector<LinearInequality> combined = poly1;
                combined.insert(combined.end(), poly2.begin(), poly2.end());
                new_result.polyhedra.push_back(combined);
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
            // (* a b) — one operand must be a constant, the other an expression
            // Handles both (* constant expr) and (* expr constant)
            if (SExprUtils::isNumericLiteral(parts[1])) {
                double coef = std::stod(parts[1]);
                AffineTerm inner = parseArithExpr(parts[2]);
                inner *= coef;
                return inner;
            } else if (SExprUtils::isNumericLiteral(parts[2])) {
                double coef = std::stod(parts[2]);
                AffineTerm inner = parseArithExpr(parts[1]);
                inner *= coef;
                return inner;
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