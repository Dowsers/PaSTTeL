#include <sstream>
#include <iostream>

#include "linearization/array_handler.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR + INTERFACE EXISTANTE
// ============================================================================

ArrayHandler::ArrayHandler(const std::string& default_element_sort)
    : m_default_element_sort(default_element_sort)
{
}

bool ArrayHandler::canHandle(const std::string& op) const {
    // Phase 2 : seul select est abstrait en variable fraiche.
    // store est elimine en phase 1 (preprocessing).
    return op == "select";
}

std::string ArrayHandler::getPrefix() const {
    return "arr__";
}

std::string ArrayHandler::getSort(const std::string& /*op*/,
                                  const std::vector<std::string>& /*args*/) const {
    return m_default_element_sort;
}

std::string ArrayHandler::getName() const {
    return "ArrayHandler";
}

// ============================================================================
// UTILITAIRES
// ============================================================================

// splitSExpr() and trim() are now inline in the header, delegating to SExprUtils.

// ============================================================================
// PREPROCESSING : point d'entree
// ============================================================================

std::string ArrayHandler::preprocessFormula(const std::string& formula) const {
    std::string trimmed = trim(formula);
    if (trimmed.empty() || trimmed == "true") return formula;

    // Pas de store dans la formule ? Rien a faire.
    if (trimmed.find("store") == std::string::npos) return formula;

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    if (verbose) {
        std::cout << "  [ArrayHandler] Preprocessing store expressions..." << std::endl;
    }

    // Etape 1 : simplifier (select (store ...) ...) par read-over-write
    std::string result = simplifySelectStore(trimmed);

    // Etape 2 : expanser les egalites store en egalites select
    result = expandStoreEqualities(result);

    if (verbose && result != formula) {
        std::cout << "  [ArrayHandler] Store elimination done." << std::endl;
    }

    return result;
}

// ============================================================================
// READ-OVER-WRITE : (select (store arr idx val) j)
//   si idx == j : val
//   si idx != j : (select arr j)
// Hypothese : indices syntaxiquement differents => distincts.
// ============================================================================

std::string ArrayHandler::simplifySelectStore(const std::string& expr) {
    std::string trimmed = trim(expr);
    if (trimmed.empty() || trimmed[0] != '(') return trimmed;

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    // Recurse on all children first (bottom-up)
    std::vector<std::string> simplified;
    simplified.push_back(tokens[0]);
    for (size_t i = 1; i < tokens.size(); ++i) {
        simplified.push_back(simplifySelectStore(tokens[i]));
    }

    // Check for (select (store arr idx val) j)
    if (simplified[0] == "select" && simplified.size() == 3) {
        std::string arr_expr = simplified[1];
        std::string j = simplified[2];

        if (arr_expr.size() > 6 && arr_expr.substr(0, 6) == "(store") {
            auto store_tokens = splitSExpr(arr_expr);
            if (store_tokens.size() == 4 && store_tokens[0] == "store") {
                std::string inner_arr = store_tokens[1];
                std::string idx = store_tokens[2];
                std::string val = store_tokens[3];

                if (idx == j) {
                    // Same index: return the stored value
                    return val;
                } else {
                    // Different index: skip the store, recurse
                    return simplifySelectStore("(select " + inner_arr + " " + j + ")");
                }
            }
        }
    }

    // Rebuild
    std::ostringstream rebuilt;
    rebuilt << "(" << simplified[0];
    for (size_t i = 1; i < simplified.size(); ++i) {
        rebuilt << " " << simplified[i];
    }
    rebuilt << ")";
    return rebuilt.str();
}

// ============================================================================
// EXPANSION DES STORE-EGALITES EN SELECT-EGALITES
// ============================================================================

std::string ArrayHandler::expandStoreEqualities(const std::string& formula) {
    std::string trimmed = trim(formula);
    if (trimmed.empty() || trimmed[0] != '(' || trimmed.find("store") == std::string::npos) {
        return formula;
    }

    // Collect all concrete indices used in the entire formula
    std::set<std::string> all_indices;
    collectArrayIndices(trimmed, all_indices);

    if (all_indices.empty()) return formula;

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    if (tokens[0] == "and") {
        // Process each conjunct
        std::vector<std::string> new_conjuncts;
        for (size_t i = 1; i < tokens.size(); ++i) {
            auto expanded = expandSingleConjunct(tokens[i], all_indices);
            new_conjuncts.insert(new_conjuncts.end(), expanded.begin(), expanded.end());
        }

        if (new_conjuncts.size() == 1) return new_conjuncts[0];

        std::ostringstream result;
        result << "(and";
        for (const auto& c : new_conjuncts) {
            result << " " << c;
        }
        result << ")";
        return result.str();
    }

    // Single formula that might be a store equality
    auto expanded = expandSingleConjunct(trimmed, all_indices);
    if (expanded.size() == 1) return expanded[0];

    std::ostringstream result;
    result << "(and";
    for (const auto& c : expanded) {
        result << " " << c;
    }
    result << ")";
    return result.str();
}

std::vector<std::string> ArrayHandler::expandSingleConjunct(
    const std::string& conjunct, const std::set<std::string>& all_indices)
{
    auto tokens = splitSExpr(conjunct);
    if (tokens.size() != 3 || tokens[0] != "=") {
        return {conjunct};
    }

    // Check (= arr_new (store ...)) or (= (store ...) arr_new)
    std::string arr_new;
    std::string store_expr;

    if (tokens[2].size() > 6 && tokens[2].substr(0, 6) == "(store") {
        arr_new = tokens[1];
        store_expr = tokens[2];
    } else if (tokens[1].size() > 6 && tokens[1].substr(0, 6) == "(store") {
        arr_new = tokens[2];
        store_expr = tokens[1];
    } else {
        return {conjunct};
    }

    // arr_new must be a simple variable (not an expression)
    if (arr_new[0] == '(') {
        return {conjunct};
    }

    // For each concrete index, generate (= (select arr_new idx) evaluated_value)
    std::vector<std::string> result;
    for (const auto& idx : all_indices) {
        std::string value = evaluateStoreAtIndex(store_expr, idx);
        std::string new_select = "(select " + arr_new + " " + idx + ")";

        // Skip tautologies (value == the select on same array at same index)
        if (value == new_select) continue;

        result.push_back("(= " + new_select + " " + value + ")");
    }

    if (result.empty()) {
        return {conjunct};
    }

    return result;
}

// ============================================================================
// EVALUATION D'UNE CHAINE DE STORES A UN INDEX DONNE
// ============================================================================

std::string ArrayHandler::evaluateStoreAtIndex(
    const std::string& store_expr, const std::string& index)
{
    std::string trimmed = trim(store_expr);

    // Base case: not a store expression -> (select arr index)
    if (trimmed[0] != '(' || trimmed.find("(store") != 0) {
        return "(select " + trimmed + " " + index + ")";
    }

    auto tokens = splitSExpr(trimmed);
    if (tokens.size() != 4 || tokens[0] != "store") {
        return "(select " + trimmed + " " + index + ")";
    }

    std::string inner_arr = tokens[1];
    std::string store_idx = tokens[2];
    std::string store_val = tokens[3];

    if (store_idx == index) {
        return store_val;
    } else {
        return evaluateStoreAtIndex(inner_arr, index);
    }
}

// ============================================================================
// COLLECTE DES INDICES CONCRETS
// ============================================================================

void ArrayHandler::collectArrayIndices(
    const std::string& expr, std::set<std::string>& indices)
{
    std::string trimmed = trim(expr);
    if (trimmed.empty() || trimmed[0] != '(') return;

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return;

    // (select arr idx) -> collect idx if it's an atom
    if (tokens[0] == "select" && tokens.size() == 3) {
        if (tokens[2][0] != '(') {
            indices.insert(tokens[2]);
        }
    }

    // (store arr idx val) -> collect idx if it's an atom
    if (tokens[0] == "store" && tokens.size() == 4) {
        if (tokens[2][0] != '(') {
            indices.insert(tokens[2]);
        }
    }

    // Recurse on all children
    for (size_t i = 1; i < tokens.size(); ++i) {
        collectArrayIndices(tokens[i], indices);
    }
}
