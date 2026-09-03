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

ArrayHandler::ArrayHandler(const std::string& default_element_sort,
                            const std::atomic<bool>* cancel_flag)
    : m_default_element_sort(default_element_sort)
    , m_cancel_flag(cancel_flag)
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

void ArrayHandler::setInvariantIndexCandidates(
    const std::map<std::string, std::string>& in_vars,
    const std::map<std::string, std::string>& out_vars) const
{
    m_invariant_index_ssa.clear();
    m_array_ssa_side.clear();
    m_ssa_to_prog_var.clear();
    m_invariant_array_ssa.clear();
    m_scalar_partner.clear();
    for (const auto& [prog_var, in_ssa] : in_vars) {
        auto out_it = out_vars.find(prog_var);
        if (out_it == out_vars.end()) continue;
        const std::string& out_ssa = out_it->second;
        m_ssa_to_prog_var[in_ssa] = prog_var;
        m_ssa_to_prog_var[out_ssa] = prog_var;

        bool is_array = false;
        auto sort_it = m_var_sorts.find(prog_var);
        if (sort_it != m_var_sorts.end()) is_array = isArraySort(sort_it->second);

        if (in_ssa == out_ssa) {
            m_invariant_index_ssa.insert(in_ssa);
            // An unchanged array (in-SSA == out-SSA) has no in/out cell
            // distinction, but its reads at loop-invariant indices ARE promotable
            // as loop-invariant scalars (Ultimate promotes them just the same).
            // Record it so promoteInvariantArrayCells() can build such a cell --
            // a genuine ranking-usable variable for a bound like `#length[base]`.
            if (is_array) m_invariant_array_ssa[in_ssa] = prog_var;
            continue;
        }
        m_array_ssa_side[in_ssa] = {prog_var, true};
        m_array_ssa_side[out_ssa] = {prog_var, false};
        // Remember the opposite-side name so isIndexLoopInvariant() can probe
        // whether a non-syntactically-invariant index is still provably equal
        // across the loop.
        m_scalar_partner[in_ssa] = out_ssa;
        m_scalar_partner[out_ssa] = in_ssa;
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

std::string ArrayHandler::sortForProbe(const std::string& id, bool used_as_array) const {
    // Trust an explicitly declared sort first: the program variable itself,
    // then an aux var minted here, then the program variable this SSA name
    // stands for (setInvariantIndexCandidates() records both in/out SSA -> prog
    // var). Only with no declared sort do we fall back to the usage-based guess
    // -- and there, unlike computeSort()'s "unknown -> array" default (right for
    // a select head, wrong for a bare index), we honour how the identifier was
    // actually used in this formula.
    auto it = m_var_sorts.find(id);
    if (it != m_var_sorts.end()) return it->second;
    auto aux = m_aux_var_sorts.find(id);
    if (aux != m_aux_var_sorts.end()) return aux->second;
    auto pv = m_ssa_to_prog_var.find(id);
    if (pv != m_ssa_to_prog_var.end()) {
        auto vs = m_var_sorts.find(pv->second);
        if (vs != m_var_sorts.end()) return vs->second;
    }
    return used_as_array ? "(Array Int " + m_default_element_sort + ")"
                         : m_default_element_sort;
}

bool ArrayHandler::isSatisfiableWith(const std::string& context, const std::string& extra_assertion) const {
    std::set<std::string> scalars, arrays;
    collectIdentifiers(context, scalars, arrays);
    collectIdentifiers(extra_assertion, scalars, arrays);

    SMTSolverZ3 solver(false);
    // Declare each identifier with its most reliable sort. A declared program-
    // or aux-var sort (found directly, or via the SSA -> program-var map) always
    // wins over the usage-based scalar/array guess: an array SSA that only ever
    // appears as an `=` operand -- e.g. v_a_out in `(= v_a_out (store v_a_in ...))`,
    // never as a select/store head -- is otherwise classified scalar, so the
    // store equality becomes a Z3 type error, the whole probe is spuriously
    // UNSAT, and EVERY index pair collapses to NOT_EQUAL. That is a soundness
    // hole in classifyIndices (a genuinely-equal index pair mis-read as
    // distinct drops a frame condition), not a mere missed optimization.
    for (const auto& a : arrays) {
        solver.declareVariable(a, sortForProbe(a, /*used_as_array=*/true));
    }
    for (const auto& s : scalars) {
        solver.declareVariable(s, sortForProbe(s, /*used_as_array=*/false));
    }

    std::string ctx = SExprUtils::trim(context);
    if (!ctx.empty() && ctx != "true") solver.addAssertion(ctx);
    solver.addAssertion(extra_assertion);
    return solver.checkSat();
}

ArrayHandler::IndexRelation ArrayHandler::classifyIndices(
    const std::string& idx1, const std::string& idx2, const std::string& context) const
{
    std::string t1 = SExprUtils::trim(idx1);
    std::string t2 = SExprUtils::trim(idx2);
    if (t1 == t2) {
        return IndexRelation::EQUAL;  // syntactic fast path -- still a correct answer
    }

    // Memoized: see m_index_relation_cache's doc comment. Order-independent
    // key since the relation is symmetric in idx1/idx2.
    const std::string& lo = (t1 < t2) ? t1 : t2;
    const std::string& hi = (t1 < t2) ? t2 : t1;
    std::string cache_key = lo + '\x01' + hi;
    auto cached = m_index_relation_cache.find(cache_key);
    if (cached != m_index_relation_cache.end()) {
        return cached->second;
    }

    // Each isSatisfiableWith() call below builds a fresh throwaway solver
    // over the whole `context` formula -- unlike distributeAND's Cartesian
    // blowup, a single distinct pair can be expensive on its own for a
    // large/array-heavy context, and there is no bound on how many distinct
    // pairs a formula has even with memoization removing repeats. Same
    // reasoning as smt_parser.cpp's checkCancellation(): poll the calling
    // technique's own cancellation flag (passed in at construction -- see
    // m_cancel_flag) rather than run unboundedly once it's been told to stop.
    if (m_cancel_flag && m_cancel_flag->load(std::memory_order_relaxed)) {
        throw PreprocessingCancelledException(
            "Index-equality classification cancelled -- this instance has "
            "too many distinct array-index pairs to classify before any SMT "
            "solving on the actual problem even starts");
    }

    IndexRelation result;
    bool eq_possible = isSatisfiableWith(context, "(= " + idx1 + " " + idx2 + ")");
    if (!eq_possible) {
        result = IndexRelation::NOT_EQUAL;
    } else {
        bool neq_possible = isSatisfiableWith(context, "(not (= " + idx1 + " " + idx2 + "))");
        result = neq_possible ? IndexRelation::UNKNOWN : IndexRelation::EQUAL;
    }

    m_index_relation_cache[cache_key] = result;
    return result;
}

// ============================================================================
// PREPROCESSING : point d'entree
// ============================================================================

std::string ArrayHandler::preprocessFormula(const std::string& formula) const {
    std::string trimmed = trim(formula);
    if (trimmed.empty() || trimmed == "true") return formula;

    // SMT context for isIndexLoopInvariant()'s classifyIndices() probes: the
    // original transition formula, which carries the index-equality conjuncts.
    m_current_context = trimmed;
    // A fresh context invalidates every previously memoized verdict.
    m_index_relation_cache.clear();
    m_array_resolution_cache.clear();

    // Nothing to do if there's neither a store to eliminate nor a select to
    // possibly promote (see promoteInvariantArrayCells()).
    if (trimmed.find("store") == std::string::npos &&
        trimmed.find("select") == std::string::npos) return formula;

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

    // Etape 3 : promouvoir les cellules a index invariant en variables de
    // programme scalaires. Doit tourner apres les etapes 1-2 : c'est
    // buildArrayEqualityAtom() (appele par expandStoreEqualities) qui fait
    // apparaitre la valeur "out" comme un select litteral, via son propre
    // scoping d'indices par (array, dimension) -- voir collectIndicesForIdentity().
    result = promoteInvariantArrayCells(result);

    // Etape 4 : congruence fonctionnelle entre cellules promues d'un meme
    // tableau dont les indices sont (prouvablement) egaux -- sinon deux
    // (select a i)/(select a j) avec i==j deviennent des scalaires independants,
    // ce qui laisse GeometricTechnique fabriquer une fausse preuve de
    // non-terminaison. Le contexte est la formule d'origine (elle porte les
    // contraintes d'index comme (= v_j_5 v_i_8)), pas `result` ou les selects
    // ont deja ete remplaces. Voir buildPromotedCellCongruence().
    std::vector<std::string> congruence = buildPromotedCellCongruence(trimmed, result);
    if (!congruence.empty()) {
        std::ostringstream oss;
        oss << "(and " << result;
        for (const auto& c : congruence) oss << " " << c;
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
                // Memoized: see m_array_resolution_cache's doc comment. Must
                // be checked before classifyIndices()/the recursive `other`
                // call below -- those are exactly the expensive work a repeat
                // occurrence of the same (arr_expr, j) pair needs to skip.
                std::string cache_key = arr_expr + '\x01' + j;
                auto cached = m_array_resolution_cache.find(cache_key);
                if (cached != m_array_resolution_cache.end()) {
                    return cached->second;
                }

                std::string inner_arr = store_tokens[1];
                std::string idx = store_tokens[2];
                std::string val = store_tokens[3];

                IndexRelation rel = classifyIndices(idx, j, context);
                std::string result;
                if (rel == IndexRelation::EQUAL) {
                    result = val;
                } else if (rel == IndexRelation::NOT_EQUAL) {
                    result = simplifySelectStore("(select " + inner_arr + " " + j + ")", context, extra);
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
                    result = aux;
                }
                m_array_resolution_cache[cache_key] = result;
                return result;
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

bool ArrayHandler::isIndexLoopInvariant(const std::string& index_ssa) const {
    // Syntactically unchanged index (in-SSA == out-SSA name).
    if (m_invariant_index_ssa.count(index_ssa)) return true;

    // Numeric literal -- a constant, hence trivially loop-invariant.
    {
        size_t i = (!index_ssa.empty() && index_ssa[0] == '-') ? 1 : 0;
        bool all_digits = i < index_ssa.size();
        for (; i < index_ssa.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(index_ssa[i]))) { all_digits = false; break; }
        if (all_digits) return true;
    }

    // Distinct in/out SSA names, but the loop body may still force them equal
    // (e.g. `(= v_base_156 v_base_155)`). Ask classifyIndices() -- the SMT-based
    // IndexAnalyzer -- against the current transition.
    auto p = m_scalar_partner.find(index_ssa);
    if (p != m_scalar_partner.end()) {
        return classifyIndices(index_ssa, p->second, m_current_context) == IndexRelation::EQUAL;
    }
    return false;
}

std::string ArrayHandler::promoteInvariantArrayCells(const std::string& expr) const {
    std::string trimmed = trim(expr);
    if (trimmed.empty() || trimmed[0] != '(') return trimmed;

    auto tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    std::vector<std::string> rebuilt;
    rebuilt.push_back(tokens[0]);
    for (size_t i = 1; i < tokens.size(); ++i) {
        rebuilt.push_back(promoteInvariantArrayCells(tokens[i]));
    }

    if (rebuilt[0] == "select" && rebuilt.size() == 3) {
        const std::string& base_ssa = rebuilt[1];
        const std::string& index_ssa = rebuilt[2];
        bool base_is_plain = !base_ssa.empty() && base_ssa[0] != '(';
        bool index_is_plain = !index_ssa.empty() && index_ssa[0] != '(';

        if (base_is_plain && index_is_plain && isIndexLoopInvariant(index_ssa)) {
            // Two array kinds are promotable (Ultimate treats both uniformly):
            //  - a MUTATED array (distinct in/out SSA): the cell is loop-carried,
            //    its value differs in/out -> distinct in_ssa/out_ssa, the read
            //    resolves to whichever side this SSA version is;
            //  - an UNCHANGED array (in-SSA == out-SSA) read at a loop-invariant
            //    index: the cell is a loop CONSTANT -> a single in==out scalar,
            //    the ranking-usable variable for a bound like `#length[base]`.
            auto side_it = m_array_ssa_side.find(base_ssa);
            auto invarr_it = m_invariant_array_ssa.find(base_ssa);
            bool array_mutated  = (side_it  != m_array_ssa_side.end());
            bool array_constant = (invarr_it != m_invariant_array_ssa.end());

            if (array_mutated || array_constant) {
                const std::string& array_prog_var =
                    array_mutated ? side_it->second.first : invarr_it->second;

                std::string elem_sort = peelArrayDimension(computeSort(base_ssa));
                if (elem_sort == "Int" || elem_sort == "Real") {
                    // Key by the index's PROGRAM VARIABLE, not its SSA name, so
                    // the SAME cell a[k] read in the stem (index SSA v_k_9) and in
                    // the loop (index SSA v_k_10) maps to ONE cell variable --
                    // this is Ultimate's array/index representative keying. Keying
                    // by raw SSA would split a[k] into two independent variables
                    // and let GeometricTechnique pick different values in stem and
                    // loop, ignoring a stem constraint like `a[k] >= 1` and
                    // fabricating non-termination (the "CommonCellVariable" case).
                    auto idx_pv_it = m_ssa_to_prog_var.find(index_ssa);
                    std::string index_rep =
                        idx_pv_it != m_ssa_to_prog_var.end() ? idx_pv_it->second : index_ssa;
                    std::string key = array_prog_var + "@" + index_rep;
                    auto cell_it = m_promoted_cell_index.find(key);
                    size_t idx_in_vec;
                    if (cell_it == m_promoted_cell_index.end()) {
                        PromotedCell cell;
                        cell.pseudo_var = "arrcell__" + array_prog_var + "__" + index_rep;
                        if (array_constant) {
                            // Loop-invariant cell: one name for both sides.
                            cell.in_ssa = cell.out_ssa = cell.pseudo_var + "__inv";
                        } else {
                            cell.in_ssa = cell.pseudo_var + "__in";
                            cell.out_ssa = cell.pseudo_var + "__out";
                        }
                        cell.sort = elem_sort;
                        cell.array_name = array_prog_var;
                        cell.index_ssa = index_ssa;
                        cell.index_display.push_back(index_rep);
                        m_promoted_cells.push_back(cell);
                        idx_in_vec = m_promoted_cells.size() - 1;
                        m_promoted_cell_index[key] = idx_in_vec;
                    } else {
                        idx_in_vec = cell_it->second;
                    }
                    const PromotedCell& cell = m_promoted_cells[idx_in_vec];
                    // Constant cell: in_ssa == out_ssa, so side is irrelevant.
                    bool is_in_side = array_mutated ? side_it->second.second : true;
                    return is_in_side ? cell.in_ssa : cell.out_ssa;
                }
            }
        }
    }

    std::ostringstream oss;
    oss << "(" << rebuilt[0];
    for (size_t i = 1; i < rebuilt.size(); ++i) oss << " " << rebuilt[i];
    oss << ")";
    return oss.str();
}

// ============================================================================
// CONGRUENCE FONCTIONNELLE (ACKERMANN) ENTRE CELLULES PROMUES
// ============================================================================

std::vector<std::string> ArrayHandler::buildPromotedCellCongruence(
    const std::string& context, const std::string& referenced) const
{
    std::vector<std::string> atoms;

    // Group promoted cells by array program variable; only same-array cells can
    // alias. Sets are tiny (one entry per distinct invariant index of an array),
    // so the O(n^2) pairing below issues only a handful of classifyIndices()
    // SMT queries per array -- the same cost model the store path already pays.
    // m_promoted_cells is cumulative across transitions, so keep only the cells
    // whose (unique) scalar name occurs in this transition's formula.
    std::map<std::string, std::vector<const PromotedCell*>> by_array;
    for (const auto& cell : m_promoted_cells) {
        if (referenced.find(cell.in_ssa) == std::string::npos &&
            referenced.find(cell.out_ssa) == std::string::npos) {
            continue;
        }
        by_array[cell.array_name].push_back(&cell);
    }

    for (const auto& [array_name, cells] : by_array) {
        (void)array_name;
        for (size_t a = 0; a < cells.size(); ++a) {
            for (size_t b = a + 1; b < cells.size(); ++b) {
                const PromotedCell* ca = cells[a];
                const PromotedCell* cb = cells[b];
                // Distinct PromotedCells always have distinct index SSA (the
                // (array,index) key dedups), so this is a genuine index pair.
                IndexRelation rel = classifyIndices(ca->index_ssa, cb->index_ssa, context);
                if (rel == IndexRelation::NOT_EQUAL) continue;

                if (rel == IndexRelation::EQUAL) {
                    // Indices provably equal -> same cell in every state.
                    // Unconditional equality on both sides; no case split.
                    atoms.push_back(eq(ca->in_ssa, cb->in_ssa));
                    atoms.push_back(eq(ca->out_ssa, cb->out_ssa));
                } else {  // UNKNOWN -> guarded Ackermann implication, per side.
                    atoms.push_back("(or " + neq(ca->index_ssa, cb->index_ssa)
                                    + " " + eq(ca->in_ssa, cb->in_ssa) + ")");
                    atoms.push_back("(or " + neq(ca->index_ssa, cb->index_ssa)
                                    + " " + eq(ca->out_ssa, cb->out_ssa) + ")");
                }
            }
        }
    }

    return atoms;
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

    // Memoized: see m_array_resolution_cache's doc comment. Must be checked
    // before classifyIndices()/the recursive `other` call below -- those are
    // exactly the expensive work a repeat occurrence of the same
    // (store_expr, index) pair needs to skip. This cache is shared with
    // simplifySelectStore(), which also calls this function's sibling logic
    // via buildArrayEqualityAtom()'s per-dimension recursion.
    std::string cache_key = trimmed + '\x01' + index;
    auto cached = m_array_resolution_cache.find(cache_key);
    if (cached != m_array_resolution_cache.end()) {
        return cached->second;
    }

    std::string inner_arr = tokens[1];
    std::string store_idx = tokens[2];
    std::string store_val = tokens[3];

    IndexRelation rel = classifyIndices(store_idx, index, context);
    std::string result;
    if (rel == IndexRelation::EQUAL) {
        result = store_val;
    } else if (rel == IndexRelation::NOT_EQUAL) {
        result = evaluateStoreAtIndex(inner_arr, index, context, extra);
    } else {
        std::string other = evaluateStoreAtIndex(inner_arr, index, context, extra);
        std::string aux = freshAuxVar(peelArrayDimension(computeSort(inner_arr)));
        extra.push_back("(or " + neq(store_idx, index) + " "
                         + buildArrayEqualityAtom(aux, store_val, context, extra) + ")");
        extra.push_back("(or " + eq(store_idx, index) + " "
                         + buildArrayEqualityAtom(aux, other, context, extra) + ")");
        result = aux;
    }
    m_array_resolution_cache[cache_key] = result;
    return result;
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
