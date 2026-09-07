#ifndef ARRAY_HANDLER_H
#define ARRAY_HANDLER_H

#include <atomic>
#include <map>
#include <set>
#include <utility>
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

    // cancel_flag: forwarded to classifyIndices()'s own cancellation check
    // (see its doc comment) -- optional so callers outside a cancellable
    // technique (tests, one-off tools) don't have to pass anything.
    explicit ArrayHandler(const std::string& default_element_sort = "Int",
                           const std::atomic<bool>* cancel_flag = nullptr);

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
     * @brief A single loop-carried array cell promoted to a genuine scalar
     * program variable -- see setInvariantIndexCandidates()/getPromotedCells().
     */
    struct PromotedCell {
        std::string pseudo_var;  // synthesized program-var name, e.g. "arrcell__A__v_i_2"
        std::string in_ssa;      // fresh SSA name standing in for the cell's value on entry
        std::string out_ssa;     // fresh SSA name standing in for the cell's value on exit
        std::string sort;        // element sort (always "Int" or "Real" -- see promoteInvariantArrayCells())

        // Structured display info, kept separate from pseudo_var's flat name
        // so a reader (e.g. LassoProgram::prettyVarName()) can render it as
        // "A[i]" instead of the raw internal identifier. index_display has
        // exactly one entry today (depth-1 promotion only); it's a vector,
        // not a single string, so a future multi-dimensional promotion
        // (A[i][j]) needs no change here or in any reader of this struct --
        // only promoteInvariantArrayCells() itself would need to populate
        // more than one entry.
        std::string array_name;                  // e.g. "A" -- program-var level, not an SSA name
        std::string index_ssa;                   // e.g. "v_i_2" -- the (invariant) index SSA term
        std::vector<std::string> index_display;   // e.g. {"i"} -- readable index name(s)
    };

    /**
     * @brief Declares this transition's own program-var -> in/out SSA name
     * mapping, so preprocessFormula() can recognize when an index used in a
     * `(select array index)` is provably loop-invariant (same SSA value on
     * both sides of the transition) and when a `(select ...)` argument is
     * literally this transition's declared in- or out-SSA name for some
     * array-sorted program variable. Both are required to safely promote an
     * array cell to a real loop-carried state variable -- see
     * promoteInvariantArrayCells() for why invariance matters. Call once per
     * transition, before preprocessFormula(); mirrors setVarSorts()'s pattern
     * of per-transition context the class doesn't otherwise have visibility
     * into (ArrayHandler operates on formula text alone).
     */
    void setInvariantIndexCandidates(const std::map<std::string, std::string>& in_vars,
                                      const std::map<std::string, std::string>& out_vars) const;

    /**
     * @brief Array cells promoted by the last preprocessFormula() call(s)
     * (cumulative across every transition this instance has processed, like
     * getAuxVarNames() -- callers must filter to the ones actually referenced
     * in the transition they care about, e.g. by checking whether in_ssa/
     * out_ssa appears in that transition's own (post-preprocessing) formula
     * text). The caller must register each one as a genuine program variable
     * (program_vars + var_to_ssa_in/out + var_sorts) -- unlike getAuxVarNames()'s
     * one-shot scalars, these represent state that persists across the loop
     * transition and must get the same treatment as any other loop variable
     * for techniques that reason about repeated loop iterations (GeometricTechnique).
     */
    const std::vector<PromotedCell>& getPromotedCells() const { return m_promoted_cells; }

    /**
     * @brief Decides whether idx1==idx2 given the constraints in `context`,
     * via real SMT queries against a throwaway solver -- never assumes
     * syntactic difference means semantic difference.
     *
     * Memoized per (idx1, idx2) pair for the current preprocessFormula()
     * call (see m_index_relation_cache): a chain of stores/selects on the
     * same array routinely asks the same handful of index pairs dozens of
     * times over (confirmed: 6 underlying pairs asked ~42 times each on one
     * real instance), each occurrence otherwise paying for two fresh SMT
     * solver round-trips to re-derive an answer that cannot have changed --
     * `context` is fixed for the whole call, so the verdict can't either.
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

    /**
     * @brief Real sort of `identifier` IF this instance has actual evidence
     * for it (a declared var_types/array_vars entry, or a store-equality
     * target it inferred one while eliminating -- see expandSingleConjunct),
     * or empty if unknown. Unlike computeSort()'s internal fallback (which
     * assumes an unrecognized identifier is "one array layer away" because
     * every caller inside this class already knows it's looking at an array
     * expression), a caller here has no such guarantee -- e.g. a genuinely
     * scalar free variable (a div/mod aux var) must not get promoted to
     * array-sorted just because nothing is known about it.
     */
    std::string getKnownSort(const std::string& identifier) const {
        std::string trimmed = trim(identifier);
        auto it = m_var_sorts.find(trimmed);
        if (it != m_var_sorts.end()) return it->second;
        auto aux_it = m_aux_var_sorts.find(trimmed);
        if (aux_it != m_aux_var_sorts.end()) return aux_it->second;
        return "";
    }

private:
    std::string m_default_element_sort;
    const std::atomic<bool>* m_cancel_flag;

    // Real declared sort per array variable name (see setVarSorts()). Also
    // grows at preprocessing time: a store-equality's target var (e.g. "A'"
    // in "A' = (store A i v)") gets its inferred sort recorded here too, so
    // a later lookup (including from outside this class, e.g. a free/local
    // variable never covered by setVarSorts()) can find it -- see
    // getKnownSort() and expandSingleConjunct().
    mutable std::map<std::string, std::string> m_var_sorts;

    // Aux vars minted during the last preprocessFormula() call (see getAuxVarNames()).
    mutable std::vector<std::string> m_created_aux_vars;

    // Real sort per aux var name (see getAuxVarSort()).
    mutable std::map<std::string, std::string> m_aux_var_sorts;

    // name -> the array expression it was defined from (e.g. "A'" -> "A" once
    // "A' = (store A ...)" has been seen). See resolveArrayBase().
    mutable std::map<std::string, std::string> m_array_equiv_base;

    // SSA names provably unchanged across the current transition (in == out
    // for some program var) -- only such a name is safe as a promotable
    // cell's index. See setInvariantIndexCandidates().
    mutable std::set<std::string> m_invariant_index_ssa;

    // SSA name -> (program var, is_in_side) for every var declared via
    // setInvariantIndexCandidates() for the current transition. Lets
    // promoteInvariantArrayCells() recognize a `(select ssa_name ...)` whose
    // ssa_name IS this transition's own in- or out-SSA name for some array.
    mutable std::map<std::string, std::pair<std::string, bool>> m_array_ssa_side;

    // SSA name -> program var, for EVERY var declared via
    // setInvariantIndexCandidates() (not just arrays) -- lets
    // promoteInvariantArrayCells() resolve an index like "v_i_2" back to its
    // readable program-variable name ("i") for PromotedCell::index_display,
    // instead of baking the raw SSA name into what gets displayed later.
    mutable std::map<std::string, std::string> m_ssa_to_prog_var;

    // Array SSA name -> program var, for arrays UNCHANGED across the transition
    // (in-SSA == out-SSA). Ultimate promotes such loop-invariant array reads
    // too (ArrayCellRepVarConstructor treats every array cell uniformly); their
    // cells are loop-invariant (in == out), giving the ranking template a real
    // variable for a bound like `#length[base]`. See promoteInvariantArrayCells().
    mutable std::map<std::string, std::string> m_invariant_array_ssa;

    // Non-invariant scalar/array SSA name -> its opposite-side SSA name (in<->out
    // for the same program var). Lets isIndexLoopInvariant() ask classifyIndices()
    // whether an index whose in/out SSA DIFFER is nonetheless provably equal
    // across the loop (e.g. `v_base_156`/`v_base_155` tied by `(= ...)` in the
    // body) -- the semantic index invariance Ultimate's IndexAnalyzer gives.
    mutable std::map<std::string, std::string> m_scalar_partner;

    // The current transition formula (set by preprocessFormula), used as the
    // SMT context for isIndexLoopInvariant()'s classifyIndices() probes.
    mutable std::string m_current_context;

    // classifyIndices() memoization for the current preprocessFormula() call
    // -- cleared whenever m_current_context changes (a fresh transition means
    // a fresh set of constraints, so a stale verdict could be wrong). Keyed
    // by idx1+"\x01"+idx2 with idx1<=idx2 lexicographically, since EQUAL/
    // NOT_EQUAL/UNKNOWN is symmetric in its two arguments.
    mutable std::map<std::string, IndexRelation> m_index_relation_cache;

    // simplifySelectStore()/evaluateStoreAtIndex() memoization: both resolve
    // "value of this store-chain expression at this index", and without this
    // cache each REPEATS that full resolution -- including the recursive
    // descent into the chain's own inner store/select structure via `other`,
    // and (on UNKNOWN) minting a fresh aux var + emitting 2 guarded
    // disjunctions -- on every single textual occurrence of the same
    // (chain, index) pair, even though the answer can't change (`context` is
    // fixed for the whole call, same as m_index_relation_cache above). A
    // chain of stores read from several downstream points routinely asks the
    // same pair dozens of times over; confirmed root cause of a 3-store,
    // 1-UNKNOWN-pair chain minting 42 separate aux vars instead of 1. Keyed
    // by chain_expr+"\x01"+index -- NOT symmetric (chain_expr and index play
    // different roles), unlike m_index_relation_cache's key. Cleared
    // alongside it in preprocessFormula(). Shared between both functions:
    // they solve the same sub-problem, so a value resolved by one is valid
    // for the other too when their (chain_expr, index) text happens to match.
    mutable std::map<std::string, std::string> m_array_resolution_cache;

    // Promoted cells found so far, keyed by "array_prog_var@index_ssa" to
    // keep repeated occurrences of the same cell mapped to one pseudo_var.
    mutable std::vector<PromotedCell> m_promoted_cells;
    mutable std::map<std::string, size_t> m_promoted_cell_index;

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
     * @brief Follows m_array_equiv_base until reaching a name nothing else
     * was ever recorded as being derived from (cycle-safe). E.g. once
     * expandSingleConjunct() has seen "A' = (store A ...)", resolveArrayBase
     * ("A'") returns whatever A itself resolves to.
     */
    std::string resolveArrayBase(const std::string& name) const;

    /**
     * @brief (base array, dimension depth) that `expr` refers to: 0 if expr
     * IS (or resolves via m_array_equiv_base to) a base array name itself;
     * select adds one dimension, store keeps the same one (it doesn't change
     * which cells are reachable, just their values). Used to scope index
     * candidates per array per dimension (matching Ultimate MapEliminator's
     * per-`MapTemplate` `ArrayIndex` position-wise comparison) instead of
     * pooling every index in the whole formula regardless of which array or
     * dimension it's ever actually used with -- the latter cross-multiplies
     * unrelated candidates and can blow up UNKNOWN-relation case-splitting
     * for no semantic reason.
     */
    std::pair<std::string, int> computeArrayIdentity(const std::string& expr) const;

    /**
     * @brief All indices used to access exactly (array_base, dimension)
     * anywhere in `formula` -- an index only ever used for a different
     * array, or for a different dimension of the same array, is excluded.
     */
    std::set<std::string> collectIndicesForIdentity(const std::string& formula,
                                                      const std::string& array_base,
                                                      int dimension) const;

    /**
     * @brief Builds an equality atom `lhs_expr = rhs_expr`, decomposing
     * recursively (one "(select lhs_expr idx)" / evaluateStoreAtIndex(rhs,
     * idx) pair per index) as long as rhs_expr's sort is still array-valued
     * -- an array-sorted equality can't be a LinearInequality downstream, so
     * this must bottom out at scalars before eq()'s <=/>= split is valid.
     * The index candidates tried at each dimension come from
     * collectIndicesForIdentity() against rhs_expr's own (base, dimension)
     * identity, not a flat whole-formula pool.
     */
    std::string buildArrayEqualityAtom(const std::string& lhs_expr,
                                        const std::string& rhs_expr,
                                        const std::string& context,
                                        std::vector<std::string>& extra) const;

    /**
     * @brief Extracts the full multi-dimensional read index tuple from a
     * chain of nested selects `(select (select ... base i1) i2) ... iN)`,
     * matching Ultimate LassoRanker's MultiDimensionalSelect: walks from the
     * outermost select inward, collecting indices in natural [i1, ..., iN]
     * order (i1 = innermost/first dimension). `base` is whatever remains
     * once no more top-level selects can be peeled -- the array variable, or
     * a `(store ...)` chain if this read sits directly over a write.
     */
    static void extractReadTuple(const std::string& expr, std::string& base,
                                  std::vector<std::string>& indices);

    /**
     * @brief Extracts the full multi-dimensional write index tuple + value
     * from a nested store chain, matching Ultimate's MultiDimensionalStore:
     * `(store arr i1 (store (select arr i1) i2 (store (select (select arr
     * i1) i2) i3 ... val)))` is a single write to arr[i1][i2][i3] := val.
     * Peels one store per iteration, but only continues past the first if
     * the inner store's OWN array operand is exactly "(select ... (select
     * base indices-so-far) ...)" -- i.e. genuinely the row/cell just read
     * before being rewritten (Ultimate's isCompatibleSelect check) --
     * degrading gracefully (stopping early) for anything else, rather than
     * assuming every nested store belongs to the same multi-dim write.
     * `value_at_depth[k]` is the value once indices[0..k] (k+1 dimensions)
     * have been applied -- not just the final value -- so a caller comparing
     * against a shallower read tuple than the full write depth still gets
     * the right (possibly still array-sorted) intermediate value, not the
     * final scalar.
     */
    static void extractWriteTuple(const std::string& expr, std::string& base,
                                   std::vector<std::string>& indices,
                                   std::vector<std::string>& value_at_depth);

    /**
     * @brief Component-wise index-tuple relation (Ultimate's IndexAnalyzer
     * applied to a whole ArrayIndex, not one dimension): NOT_EQUAL if any
     * component (up to min(t1.size(), t2.size())) is provably distinct,
     * EQUAL only if every one of those components is provably equal,
     * UNKNOWN otherwise.
     */
    IndexRelation compareIndexTuples(const std::vector<std::string>& t1,
                                      const std::vector<std::string>& t2,
                                      const std::string& context) const;

    /**
     * @brief Resolves `(select store_chain_expr read_indices[0]) ...
     * read_indices[N-1])` -- a read applying 2+ indices on top of whatever
     * store_chain_expr resolves to -- by comparing the FULL index tuples
     * (see compareIndexTuples()), not one dimension at a time. This is what
     * actually fixes the bug the single-dimension path had: comparing only
     * the outermost index against a multi-dimensional store (Ultimate's
     * #memory_int base->offset heap model) could only ever classify THAT one
     * dimension, so an UNKNOWN verdict minted an aux var standing for "the
     * row" -- still array-sorted, since only one of the array's dimensions
     * had been peeled -- which then leaked into code expecting a scalar
     * term. Comparing the whole tuple at once means any aux var minted here
     * is exactly the dereferenced element sort, never an intermediate array.
     * Memoized like simplifySelectStore()/evaluateStoreAtIndex() -- see
     * m_array_resolution_cache.
     */
    std::string resolveMultiDimSelect(const std::string& store_chain_expr,
                                       const std::vector<std::string>& read_indices,
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
        const std::string& conjunct,
        const std::string& context, std::vector<std::string>& extra) const;

    /**
     * @brief Promotes `(select base_ssa index_ssa)` occurrences to a plain
     * scalar pseudo-variable when: base_ssa is literally this transition's
     * declared in- or out-SSA name for some array-sorted program variable
     * (via setInvariantIndexCandidates(), not a nested select/store -- only
     * depth-1 array cells are promoted, nested/multi-dimensional identities
     * are left as-is), index_ssa is loop-invariant, and the element sort is
     * "Int" or "Real" (GeometricTechnique::declareVariables() assumes one
     * uniform numeric sort for every program variable it tracks -- promoting
     * a Bool or nested-Array element would mis-type that). Must run AFTER
     * simplifySelectStore()/expandStoreEqualities(): those steps are what
     * make the "out" side's value appear as a literal select in the first
     * place (via buildArrayEqualityAtom()'s per-identity index scoping --
     * see collectIndicesForIdentity()), not something this method derives
     * itself. Every distinct (array, index) identity maps to one PromotedCell,
     * recorded in m_promoted_cells/m_promoted_cell_index regardless of how
     * many occurrences are rewritten.
     */
    std::string promoteInvariantArrayCells(const std::string& expr) const;

    /**
     * @brief Functional-consistency (Ackermann) congruence over promoted cells.
     * Two cells of the same array whose indices are provably equal denote the
     * same memory location and must hold the same value; promotion keys cells on
     * the syntactic index name alone, so without this a `(select a i)` and a
     * `(select a j)` with `i == j` entailed become independent scalars -- letting
     * GeometricTechnique fabricate a spurious non-termination witness (see the
     * "IndexEqualityInformationInLoop" case). For every pair of cells of one
     * array this emits, reusing classifyIndices() against `context`:
     *   EQUAL     -> `(= cellA cellB)` on both in- and out-sides (no disjunction);
     *   UNKNOWN   -> `(or (not (= idxA idxB)) (= cellA cellB))`, the guarded
     *                Ackermann implication, mirroring evaluateStoreAtIndex();
     *   NOT_EQUAL -> nothing.
     * This is the read-read analogue of the store path's existing congruence and
     * matches Ultimate MapEliminator's index-equality handling. Returns the list
     * of extra conjuncts to AND into the preprocessed formula.
     *
     * @param context   the ORIGINAL (pre-promotion) transition formula, carrying
     *                  the index constraints classifyIndices() reasons over.
     * @param referenced the post-promotion formula; m_promoted_cells is
     *                  cumulative across every transition this instance handled,
     *                  so only cells whose scalar name actually appears here
     *                  belong to the current transition and are paired.
     */
    std::vector<std::string> buildPromotedCellCongruence(
        const std::string& context, const std::string& referenced) const;

    /** Fresh aux var name for an unresolved select-of-store value, of sort `sort`. */
    std::string freshAuxVar(const std::string& sort) const;

    /**
     * @brief True if `context` AND `extra_assertion` are jointly satisfiable
     * (via a throwaway Z3 instance; identifiers are auto-declared).
     */
    bool isSatisfiableWith(const std::string& context, const std::string& extra_assertion) const;

    /**
     * @brief Best sort to declare `id` with for an isSatisfiableWith() probe:
     * a declared program/aux-var sort (directly or via the SSA -> program-var
     * map) if known, else the usage-based guess (`used_as_array` decides). Keeps
     * an array SSA that only appears as an `=` operand from being mis-typed
     * scalar -- see isSatisfiableWith() for why that would be unsound.
     */
    std::string sortForProbe(const std::string& id, bool used_as_array) const;

    /**
     * @brief True if the index term `index_ssa` has the same value on loop entry
     * and exit -- the condition for its array cell to be a loop-carried (or
     * loop-invariant) scalar. This is Ultimate's IndexAnalyzer notion, applied
     * semantically, not syntactically: an index is invariant if its in/out SSA
     * are the identical name, if it is a numeric literal, or if classifyIndices()
     * proves its in-SSA equal to its out-SSA against the transition (e.g. two
     * distinct SSA names tied by an `(= ...)` conjunct in the loop body).
     */
    bool isIndexLoopInvariant(const std::string& index_ssa) const;

    static std::string trim(const std::string& s) {
        return SExprUtils::trim(s);
    }
};

#endif // ARRAY_HANDLER_H
