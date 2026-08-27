#include <sstream>
#include <iostream>
#include <atomic>
#include <cctype>

#include "linearization/array_handler.h"
#include "smtsolvers/SMTSolverZ3.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

namespace {
// RewriteEquality runs before ArrayHandler, so any "=" atom minted here would
// never get its usual (<=,>=) split and the downstream DNF parser rejects
// bare "=". Pre-split it ourselves, matching what RewriteEquality would do.
std::string eq(const std::string& a, const std::string& b) {
    return "(and (<= " + a + " " + b + ") (>= " + a + " " + b + "))";
}
// Same reasoning for disequality: avoid emitting "(not (= a b))".
std::string neq(const std::string& a, const std::string& b) {
    return "(or (< " + a + " " + b + ") (> " + a + " " + b + "))";
}
}  // namespace

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

std::string ArrayHandler::getSort(const std::string& op,
                                  const std::vector<std::string>& args) const {
    if (op == "select" && !args.empty()) {
        return peelArrayDimension(computeSort(args[0]));
    }
    return m_default_element_sort;
}

std::string ArrayHandler::peelArrayDimension(const std::string& sort) const {
    std::string trimmed = trim(sort);
    if (trimmed.size() > 6 && trimmed.substr(0, 6) == "(Array") {
        auto tokens = splitSExpr(trimmed);
        if (tokens.size() == 3) return tokens[2];
    }
    // Not an array sort (already a scalar leaf, or unrecognized) -- nothing to peel.
    return trimmed;
}

bool ArrayHandler::isArraySort(const std::string& sort) const {
    std::string trimmed = trim(sort);
    return trimmed.size() > 6 && trimmed.substr(0, 6) == "(Array";
}

std::string ArrayHandler::resolveArrayBase(const std::string& name) const {
    std::string current = trim(name);
    std::set<std::string> seen;
    while (true) {
        auto it = m_array_equiv_base.find(current);
        if (it == m_array_equiv_base.end()) return current;
        if (!seen.insert(current).second) return current;  // cycle guard
        current = it->second;
    }
}

std::pair<std::string, int> ArrayHandler::computeArrayIdentity(const std::string& expr) const {
    std::string trimmed = trim(expr);

    if (trimmed.empty() || trimmed[0] != '(') {
        return {resolveArrayBase(trimmed), 0};
    }

    auto tokens = splitSExpr(trimmed);
    if (!tokens.empty()) {
        if (tokens[0] == "select" && tokens.size() == 3) {
            auto inner = computeArrayIdentity(tokens[1]);
            return {inner.first, inner.second + 1};
        }
        if (tokens[0] == "store" && tokens.size() == 4) {
            return computeArrayIdentity(tokens[1]);  // store preserves identity
        }
    }
    // Unrecognized compound expression -- treat itself as its own base
    // (shouldn't normally arise for a genuinely array-valued expression).
    return {trimmed, 0};
}

std::set<std::string> ArrayHandler::collectIndicesForIdentity(
    const std::string& formula, const std::string& array_base, int dimension) const
{
    std::set<std::string> result;
    std::string trimmed = trim(formula);
    if (trimmed.empty() || trimmed[0] != '(') return result;

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return result;

    if ((tokens[0] == "select" && tokens.size() == 3) ||
        (tokens[0] == "store" && tokens.size() == 4)) {
        auto identity = computeArrayIdentity(tokens[1]);
        if (identity.first == array_base && identity.second == dimension
            && !tokens[2].empty() && tokens[2][0] != '(') {
            result.insert(tokens[2]);
        }
    }

    for (size_t i = 1; i < tokens.size(); ++i) {
        std::set<std::string> sub = collectIndicesForIdentity(tokens[i], array_base, dimension);
        result.insert(sub.begin(), sub.end());
    }
    return result;
}

std::string ArrayHandler::buildArrayEqualityAtom(
    const std::string& lhs_expr, const std::string& rhs_expr,
    const std::string& context, std::vector<std::string>& extra) const
{
    if (!isArraySort(computeSort(rhs_expr))) {
        return eq(lhs_expr, rhs_expr);
    }

    // Only case over indices this SAME array is actually accessed at, at this
    // SAME dimension, anywhere in the formula -- matches Ultimate MapEliminator's
    // per-MapTemplate, position-wise ArrayIndex comparison (see class doc).
    // Pooling every index seen anywhere in the whole formula regardless of
    // which array/dimension it's used with cross-multiplies unrelated
    // candidates and can blow up UNKNOWN-relation case-splitting for no
    // semantic reason.
    auto identity = computeArrayIdentity(rhs_expr);
    std::set<std::string> candidates = collectIndicesForIdentity(context, identity.first, identity.second);

    std::vector<std::string> parts;
    for (const auto& idx : candidates) {
        std::string value = evaluateStoreAtIndex(rhs_expr, idx, context, extra);
        std::string new_lhs = "(select " + lhs_expr + " " + idx + ")";
        if (value == new_lhs) continue;  // tautology: unaffected read of the same cell
        parts.push_back(buildArrayEqualityAtom(new_lhs, value, context, extra));
    }

    if (parts.empty()) return eq(lhs_expr, rhs_expr);  // no concrete indices to case over
    if (parts.size() == 1) return parts[0];

    std::ostringstream oss;
    oss << "(and";
    for (const auto& p : parts) oss << " " << p;
    oss << ")";
    return oss.str();
}

std::string ArrayHandler::computeSort(const std::string& expr) const {
    std::string trimmed = trim(expr);

    auto it = m_var_sorts.find(trimmed);
    if (it != m_var_sorts.end()) return it->second;

    // Also check aux vars this same instance already minted (e.g. a chain of
    // stores: the outer level's case-split aux var becomes the "inner_arr"
    // fed into the next recursive level) -- without this, a previously
    // correctly-typed (possibly scalar) aux var looks like "just another
    // unrecognized identifier" and gets wrongly assumed array-valued.
    auto aux_it = m_aux_var_sorts.find(trimmed);
    if (aux_it != m_aux_var_sorts.end()) return aux_it->second;

    if (trimmed.size() > 1 && trimmed[0] == '(') {
        auto tokens = splitSExpr(trimmed);
        if (!tokens.empty()) {
            if (tokens[0] == "select" && tokens.size() == 3) {
                return peelArrayDimension(computeSort(tokens[1]));
            }
            if (tokens[0] == "store" && tokens.size() == 4) {
                // store's result is the same array sort as what it writes into.
                return computeSort(tokens[1]);
            }
        }
        // Any other compound expression (arithmetic, ite, etc.) -- scalar.
        return m_default_element_sort;
    }

    // A numeral (optional leading '-', otherwise all digits) is definitely
    // scalar, never "one more array layer away" -- distinct from a genuinely
    // unrecognized identifier below (e.g. buildArrayEqualityAtom() calls this
    // on a stored *value*, which is often a plain constant like "4").
    {
        size_t i = (!trimmed.empty() && trimmed[0] == '-') ? 1 : 0;
        bool all_digits = i < trimmed.size();
        for (; i < trimmed.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) { all_digits = false; break; }
        }
        if (all_digits) return m_default_element_sort;
    }

    // Unrecognized identifier (fresh var from another handler, unrelated SSA
    // var, etc.) -- assume it's being used as an array one layer from a
    // scalar, matching the flat-array default this replaces.
    return "(Array Int " + m_default_element_sort + ")";
}

std::string ArrayHandler::getName() const {
    return "ArrayHandler";
}

// ============================================================================
// UTILITAIRES
// ============================================================================

// splitSExpr() and trim() are now inline in the header, delegating to SExprUtils.

std::string ArrayHandler::freshAuxVar(const std::string& sort) const {
    static std::atomic<int> s_counter{0};
    std::string name = "arr__ite__" + std::to_string(s_counter++);
    m_created_aux_vars.push_back(name);
    m_aux_var_sorts[name] = sort;
    return name;
}

// ============================================================================
// CLASSIFICATION D'INDEX -- decision SMT reelle, jamais une supposition
// syntaxique. Matches Ultimate LassoRanker's IndexAnalyzer.
// ============================================================================

namespace {

bool isIdentifierAtom(const std::string& s) {
    if (s.empty() || s == "true" || s == "false") return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return true;  // has a non-digit -> identifier
    }
    return false;  // all digits (with optional leading '-') -> numeral
}

// Collects free identifiers from an S-expression, classifying each as a
// scalar or an array based on whether it's ever used as select/store's
// first argument. Best-effort: this is only used to declare variables for
// throwaway satisfiability probes, not for the real analysis.
void collectIdentifiers(const std::string& expr, std::set<std::string>& scalars,
                        std::set<std::string>& arrays) {
    std::string t = SExprUtils::trim(expr);
    if (t.empty()) return;
    if (t[0] != '(') {
        if (isIdentifierAtom(t) && !arrays.count(t)) scalars.insert(t);
        return;
    }

    auto tokens = SExprUtils::splitSExpr(t);
    if (tokens.empty()) return;

    size_t start = 1;
    if ((tokens[0] == "select" || tokens[0] == "store") && tokens.size() >= 2) {
        std::string arr_tok = SExprUtils::trim(tokens[1]);
        if (!arr_tok.empty() && arr_tok[0] != '(' && isIdentifierAtom(arr_tok)) {
            arrays.insert(arr_tok);
            scalars.erase(arr_tok);
        } else {
            collectIdentifiers(tokens[1], scalars, arrays);
        }
        start = 2;
    }
    for (size_t i = start; i < tokens.size(); ++i) {
        collectIdentifiers(tokens[i], scalars, arrays);
    }
}

}  // namespace

bool ArrayHandler::isSatisfiableWith(const std::string& context, const std::string& extra_assertion) const {
    std::set<std::string> scalars, arrays;
    collectIdentifiers(context, scalars, arrays);
    collectIdentifiers(extra_assertion, scalars, arrays);

    SMTSolverZ3 solver(false);
    for (const auto& a : arrays) {
        // Real per-variable sort (see computeSort()) -- a hardcoded flat
        // "(Array Int elem)" here would mis-type a nested array (e.g.
        // "(Array Int (Array Int Int))"), and asserting a formula that then
        // stores into a wrongly-scalar-typed select is a Z3 type error --
        // silently corrupting this SAT probe's verdict (observed: an
        // UNKNOWN index relation misclassified as NOT_EQUAL).
        solver.declareVariable(a, computeSort(a));
    }
    for (const auto& s : scalars) {
        solver.declareVariable(s, m_default_element_sort);
    }

    std::string ctx = SExprUtils::trim(context);
    if (!ctx.empty() && ctx != "true") solver.addAssertion(ctx);
    solver.addAssertion(extra_assertion);
    return solver.checkSat();
}

ArrayHandler::IndexRelation ArrayHandler::classifyIndices(
    const std::string& idx1, const std::string& idx2, const std::string& context) const
{
    if (SExprUtils::trim(idx1) == SExprUtils::trim(idx2)) {
        return IndexRelation::EQUAL;  // syntactic fast path -- still a correct answer
    }

    bool eq_possible = isSatisfiableWith(context, "(= " + idx1 + " " + idx2 + ")");
    if (!eq_possible) return IndexRelation::NOT_EQUAL;

    bool neq_possible = isSatisfiableWith(context, "(not (= " + idx1 + " " + idx2 + "))");
    if (!neq_possible) return IndexRelation::EQUAL;

    return IndexRelation::UNKNOWN;
}

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

    // Note: m_created_aux_vars accumulates across ALL transitions handled by
    // this instance (stem + loop share one ArrayHandler) -- never cleared here.
    std::vector<std::string> extra;

    // Etape 1 : simplifier (select (store ...) ...) par read-over-write
    std::string result = simplifySelectStore(trimmed, trimmed, extra);

    // Etape 2 : expanser les egalites store en egalites select
    result = expandStoreEqualities(result, trimmed, extra);

    if (!extra.empty()) {
        std::ostringstream oss;
        oss << "(and " << result;
        for (const auto& e : extra) oss << " " << e;
        oss << ")";
        result = oss.str();
    }

    if (verbose && result != formula) {
        std::cout << "  [ArrayHandler] Before: " << formula << std::endl;
        std::cout << "  [ArrayHandler] After:  " << result << std::endl;
        std::cout << "  [ArrayHandler] Store elimination done ("
                  << m_created_aux_vars.size() << " aux var(s))." << std::endl;
    }

    return result;
}

// ============================================================================
// READ-OVER-WRITE : (select (store arr idx val) j)
//   classifyIndices(idx, j) == EQUAL     -> val
//   classifyIndices(idx, j) == NOT_EQUAL -> (select arr j)
//   classifyIndices(idx, j) == UNKNOWN   -> fresh aux var + guarded disjunctions
//                                            (idx≠j ∨ aux=val) ∧ (idx=j ∨ aux=other)
// ============================================================================

std::string ArrayHandler::simplifySelectStore(
    const std::string& expr, const std::string& context, std::vector<std::string>& extra) const
{
    std::string trimmed = trim(expr);
    if (trimmed.empty() || trimmed[0] != '(') return trimmed;

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    // Recurse on all children first (bottom-up)
    std::vector<std::string> simplified;
    simplified.push_back(tokens[0]);
    for (size_t i = 1; i < tokens.size(); ++i) {
        simplified.push_back(simplifySelectStore(tokens[i], context, extra));
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

                IndexRelation rel = classifyIndices(idx, j, context);
                if (rel == IndexRelation::EQUAL) {
                    return val;
                } else if (rel == IndexRelation::NOT_EQUAL) {
                    return simplifySelectStore("(select " + inner_arr + " " + j + ")", context, extra);
                } else {
                    std::string other = simplifySelectStore(
                        "(select " + inner_arr + " " + j + ")", context, extra);
                    // The value at one dimension's cell -- itself another
                    // (sub-)array unless inner_arr's sort has just this one
                    // dimension left, matching what getSort("select", ...)
                    // would compute for this same select.
                    std::string aux = freshAuxVar(peelArrayDimension(computeSort(inner_arr)));
                    // aux's value may itself be array-valued (a "row"), in
                    // which case a plain eq() (scalar <=/>= split) is invalid
                    // -- decompose down to scalars, same as buildArrayEqualityAtom
                    // does for the store-equality path (which also scopes its
                    // own index candidates internally, per-array/per-dimension).
                    extra.push_back("(or " + neq(idx, j) + " "
                                     + buildArrayEqualityAtom(aux, val, context, extra) + ")");
                    extra.push_back("(or " + eq(idx, j) + " "
                                     + buildArrayEqualityAtom(aux, other, context, extra) + ")");
                    return aux;
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

std::string ArrayHandler::expandStoreEqualities(
    const std::string& formula, const std::string& context, std::vector<std::string>& extra) const
{
    std::string trimmed = trim(formula);
    if (trimmed.empty() || trimmed[0] != '(' || trimmed.find("store") == std::string::npos) {
        return formula;
    }

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    // Recurse through the whole boolean structure, not just a top-level
    // "and" -- a store-equality routinely lives inside an "or" branch (an
    // if/else in the source) or under a "not", and would otherwise never be
    // found (falls through unexpanded to a raw, un-eliminated store).
    if (tokens[0] == "and" || tokens[0] == "or") {
        std::vector<std::string> parts;
        for (size_t i = 1; i < tokens.size(); ++i) {
            parts.push_back(expandStoreEqualities(tokens[i], context, extra));
        }
        std::ostringstream result;
        result << "(" << tokens[0];
        for (const auto& p : parts) result << " " << p;
        result << ")";
        return result.str();
    }
    if (tokens[0] == "not" && tokens.size() == 2) {
        return "(not " + expandStoreEqualities(tokens[1], context, extra) + ")";
    }

    // Single formula that might be a store equality (expandSingleConjunct
    // scopes its own index candidates to the equality's own array/dimension).
    auto expanded = expandSingleConjunct(trimmed, context, extra);
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
    const std::string& conjunct,
    const std::string& context, std::vector<std::string>& extra) const
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

    // arr_new's sort is exactly store_expr's -- if it's a genuinely free/local
    // variable (not covered by setVarSorts(), e.g. an intermediate array only
    // ever named via this equality, never an in/out var), this is the only
    // place that ever learns it. Record it so a later lookup -- from within
    // this class or externally via getKnownSort() -- doesn't have to guess.
    if (m_var_sorts.find(arr_new) == m_var_sorts.end()) {
        m_var_sorts[arr_new] = computeSort(store_expr);
    }

    // arr_new is DEFINED by this equality as store_expr -- record the
    // equivalence (base-level only) so index-candidate scoping treats them as
    // the same array, matching Ultimate MapEliminator's union-find over
    // observed array-equalities (mRelatedArays), not as two unrelated arrays.
    auto store_identity = computeArrayIdentity(store_expr);
    if (store_identity.second == 0 && m_array_equiv_base.find(arr_new) == m_array_equiv_base.end()) {
        m_array_equiv_base[arr_new] = store_identity.first;
    }

    // Only case over indices this array is actually accessed at, at dimension
    // 0, anywhere in the formula -- see buildArrayEqualityAtom()'s comment.
    std::set<std::string> all_indices =
        collectIndicesForIdentity(context, resolveArrayBase(arr_new), 0);

    // For each concrete index, generate (= (select arr_new idx) evaluated_value)
    std::vector<std::string> result;
    for (const auto& idx : all_indices) {
        std::string value = evaluateStoreAtIndex(store_expr, idx, context, extra);
        std::string new_select = "(select " + arr_new + " " + idx + ")";

        // Skip tautologies (value == the select on same array at same index)
        if (value == new_select) continue;

        // value may itself still be array-valued (a nested-array cell, e.g.
        // a "row" one dimension short of a scalar) -- decompose further
        // rather than emitting an array-sorted equality, which can't be a
        // LinearInequality downstream.
        result.push_back(buildArrayEqualityAtom(new_select, value, context, extra));
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
    const std::string& store_expr, const std::string& index,
    const std::string& context, std::vector<std::string>& extra) const
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

    IndexRelation rel = classifyIndices(store_idx, index, context);
    if (rel == IndexRelation::EQUAL) {
        return store_val;
    } else if (rel == IndexRelation::NOT_EQUAL) {
        return evaluateStoreAtIndex(inner_arr, index, context, extra);
    } else {
        std::string other = evaluateStoreAtIndex(inner_arr, index, context, extra);
        std::string aux = freshAuxVar(peelArrayDimension(computeSort(inner_arr)));
        extra.push_back("(or " + neq(store_idx, index) + " "
                         + buildArrayEqualityAtom(aux, store_val, context, extra) + ")");
        extra.push_back("(or " + eq(store_idx, index) + " "
                         + buildArrayEqualityAtom(aux, other, context, extra) + ")");
        return aux;
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
