#ifndef ARRAY_HANDLER_H
#define ARRAY_HANDLER_H

#include <map>
#include <set>
#include <vector>
#include "linearization/non_linear_term_handler.h"
#include "parser/sexpr_utils.h"

/**
 * Handler pour les operations sur les arrays (select/store).
 *
 * Phase 1 (preprocessing) :
 *   - Simplifie (select (store arr idx val) j) par read-over-write
 *   - Convertit (= arr_new (store arr_old idx val)) en egalites de selects
 *   - Genere les frame conditions pour les indices concrets
 *
 * Phase 2 (linearisation) :
 *   - Remplace (select array index) par des variables fraiches
 *     arr__select__0 avec assertion (= arr__select__0 (select array index))
 *
 * Index equality: two index expressions are never assumed distinct just
 * because they're syntactically different -- classifyIndices() decides via
 * real SMT queries (push/assert/check-sat/pop against the enclosing formula),
 * matching Ultimate LassoRanker's IndexAnalyzer. When the relation is
 * genuinely unknown, an aux variable + guarded disjunctions are emitted
 * instead of guessing -- see getAuxVarNames().
 *
 * Scope: elimination runs per transition formula (one stem, one loop),
 * matching Ultimate's own preprocessors -- mapElimination there also runs on
 * one already-composed formula per stem/loop, sequential CFG edges having
 * been merged upstream, before LassoRanker ever sees them. A write and a
 * later read of the same array must therefore be expressed within that one
 * formula, with the store nested directly inside the select (e.g.
 * `(select (store A i v) j)`), not spread across separate transition edges
 * referencing an intermediate array variable (`A' = store(A,i,v)` in one
 * edge, `select A' j` in another) -- that indirection isn't traced back to
 * its defining store and the read is abstracted as opaque instead.
 */
class ArrayHandler : public NonLinearTermHandler {
public:
    enum class IndexRelation { EQUAL, NOT_EQUAL, UNKNOWN };

    explicit ArrayHandler(const std::string& default_element_sort = "Int");

    // Interface NonLinearTermHandler
    bool canHandle(const std::string& op) const override;
    std::string getPrefix() const override;
    std::string getSort(const std::string& op,
                        const std::vector<std::string>& args) const override;
    std::string getName() const override;

    /**
     * Preprocessing : elimination des stores avant linearisation.
     * 1. Simplifie (select (store ...) ...) par read-over-write
     * 2. Expanse (= arr (store ...)) en egalites de selects + frame conditions
     * Any aux var created for an UNKNOWN index relation is recorded; the
     * caller must register them (see getAuxVarNames()) since they don't
     * exist in the original JSON's aux_vars list.
     */
    std::string preprocessFormula(const std::string& formula) const override;

    /**
     * @brief Names of aux vars created by the last preprocessFormula() call
     * (element-sort scalars, standing in for a select whose resolution
     * depended on an UNKNOWN index relation). Caller must declare them
     * (e.g. as function_abstractions) since they're new, not in aux_vars.
     */
    const std::vector<std::string>& getAuxVarNames() const { return m_created_aux_vars; }

    /** @brief Sort to declare aux vars with (see getAuxVarNames()). */
    const std::string& getDefaultElementSort() const { return m_default_element_sort; }

    /**
     * @brief Real sort of a specific aux var created by getAuxVarNames()
     * (falls back to getDefaultElementSort() if unknown). Needed because,
     * for a nested array, an UNKNOWN-relation case split partway through
     * resolving one dimension stands for a whole sub-array (e.g. a "row"),
     * not always a scalar -- a single default sort for every aux var this
     * instance ever creates would mis-type it.
     */
    std::string getAuxVarSort(const std::string& name) const {
        auto it = m_aux_var_sorts.find(name);
        return it != m_aux_var_sorts.end() ? it->second : m_default_element_sort;
    }

    /**
     * @brief Declares each array variable's real full sort (e.g.
     * "A" -> "(Array Int (Array Int Int))"), so getSort() can peel exactly
     * one dimension per select instead of assuming every array's element
     * is a scalar. Without this, a select into a nested array (the classic
     * C heap-memory-model idiom, base -> offset -> value) gets abstracted
     * as if it read a scalar, and a later store into that wrongly-scalar
     * variable is nonsensical downstream. Call once after construction.
     */
    void setVarSorts(const std::map<std::string, std::string>& var_sorts) {
        for (const auto& [name, sort] : var_sorts) m_var_sorts[name] = sort;
    }

    /**
     * @brief Decides whether idx1==idx2 given the constraints in `context`,
     * via real SMT queries against a throwaway solver -- never assumes
     * syntactic difference means semantic difference.
     */
    IndexRelation classifyIndices(const std::string& idx1,
                                   const std::string& idx2,
                                   const std::string& context) const;

    // Utilitaires statiques

    /** Collecte les indices concrets des select/store dans la formule */
    static void collectArrayIndices(const std::string& expr,
                                     std::set<std::string>& indices);

    /** Split S-expression en tokens de premier niveau */
    static std::vector<std::string> splitSExpr(const std::string& expr) {
        return SExprUtils::splitSExpr(expr);
    }

private:
    std::string m_default_element_sort;

    // Real declared sort per array variable name (see setVarSorts()).
    std::map<std::string, std::string> m_var_sorts;

    // Aux vars minted during the last preprocessFormula() call (see getAuxVarNames()).
    mutable std::vector<std::string> m_created_aux_vars;

    // Real sort per aux var name (see getAuxVarSort()).
    mutable std::map<std::string, std::string> m_aux_var_sorts;

    /**
     * @brief Sort of the array-valued expression `expr` (a variable name, or
     * a `(select ...)`/`(store ...)` expression -- select peels one
     * dimension off its array's sort, store's sort is that of the array it
     * writes into). Falls back to `(Array Int m_default_element_sort)` for
     * anything not traceable to a declared variable (matches the pre-N0
     * flat-array assumption for the common case).
     */
    std::string computeSort(const std::string& expr) const;

    /** Peels one "(Array <idx> <elem>)" layer, returning <elem>. */
    std::string peelArrayDimension(const std::string& sort) const;

    /** True if `sort` is "(Array ...)" -- i.e. still has dimensions left to peel. */
    bool isArraySort(const std::string& sort) const;

    /**
     * @brief Builds an equality atom `lhs_expr = rhs_expr`, decomposing
     * recursively (one "(select lhs_expr idx)" / evaluateStoreAtIndex(rhs,
     * idx) pair per index) as long as rhs_expr's sort is still array-valued
     * -- an array-sorted equality can't be a LinearInequality downstream, so
     * this must bottom out at scalars before eq()'s <=/>= split is valid.
     * `all_indices` is the set of concrete indices to case over at every
     * dimension (matches the existing flat-array behaviour of trying every
     * index seen anywhere in the formula, harmless-if-irrelevant since a
     * mismatched dimension's index just classifies NOT_EQUAL against it).
     */
    std::string buildArrayEqualityAtom(const std::string& lhs_expr,
                                        const std::string& rhs_expr,
                                        const std::set<std::string>& all_indices,
                                        const std::string& context,
                                        std::vector<std::string>& extra) const;

    /**
     * Simplifie recursivement (select (store arr idx val) j) par read-over-write.
     * `context` is the whole enclosing formula, used for index classification.
     * `extra` accumulates guarded disjunctions needed for UNKNOWN relations.
     */
    std::string simplifySelectStore(const std::string& expr,
                                     const std::string& context,
                                     std::vector<std::string>& extra) const;

    /** Evalue un store imbrique a un index donne (read-over-write). */
    std::string evaluateStoreAtIndex(const std::string& store_expr,
                                      const std::string& index,
                                      const std::string& context,
                                      std::vector<std::string>& extra) const;

    /** Convertit les egalites store en egalites de selects. */
    std::string expandStoreEqualities(const std::string& formula,
                                       const std::string& context,
                                       std::vector<std::string>& extra) const;

    std::vector<std::string> expandSingleConjunct(
        const std::string& conjunct, const std::set<std::string>& all_indices,
        const std::string& context, std::vector<std::string>& extra) const;

    /** Fresh aux var name for an unresolved select-of-store value, of sort `sort`. */
    std::string freshAuxVar(const std::string& sort) const;

    /**
     * @brief True if `context` AND `extra_assertion` are jointly satisfiable
     * (via a throwaway Z3 instance; identifiers are auto-declared).
     */
    bool isSatisfiableWith(const std::string& context, const std::string& extra_assertion) const;

    static std::string trim(const std::string& s) {
        return SExprUtils::trim(s);
    }
};

#endif // ARRAY_HANDLER_H
