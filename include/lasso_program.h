#ifndef __LASSO_PROGRAM_H
#define __LASSO_PROGRAM_H

#include <sstream>
#include <atomic>
#include <memory>

#include "transition.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "utiles.h"

// Defined in lasso_program.cpp.
struct LinearizationCache;


// Structure pour une constante déclarée
struct DeclaredConstant {
    std::string name;
    std::string type;   // "Int", "Bool", "Ref", etc.
    std::string value;  // Valeur optionnelle pour documentation
};

// Structure pour une fonction non interprétée
struct UninterpretedFunction {
    std::string name;
    std::string signature;  // Format SMT-LIB: "(Int Int) Bool" ou "Array Int"
};

// Structure pour un axiome
struct Axiom {
    std::string formula;     // La formule SMT-LIB2 (sans le assert)
    std::string description; // Description optionnelle
};

// Display info for a promoted array cell (ArrayHandler::PromotedCell) -- lets
// LassoProgram::prettyVarName() print "A[i]" instead of the raw internal
// pseudo-variable name. index_display is a vector (not a single string) so a
// future multi-dimensional array-cell promotion (A[i][j]) needs no change
// here: only the promotion code that populates this would grow the vector.
struct ArrayCellInfo {
    std::string array_name;
    std::vector<std::string> index_display;
};

// Lasso = stem ; loop*
class LassoProgram {
public:
    LinearTransition stem;
    LinearTransition loop;
    std::vector<std::string> program_vars;

    // Constantes symboliques (ERC20, null, true, false, etc.)
    std::vector<DeclaredConstant> constants;

    // Fonctions non interprétées (sum__balances, DType, etc.)
    std::vector<UninterpretedFunction> functions;

    // Axiomes (forall, injectivité, propriétés de fonctions, etc.)
    std::vector<Axiom> axioms;

    // Abstractions de fonctions créées par la linéarisation
    std::vector<FunctionAbstraction> function_abstractions;

    // Sorts des variables de programme (par défaut "Int")
    std::map<std::string, std::string> var_sorts;

    // Display info for promoted array cells, keyed like var_sorts (by
    // pseudo_var). See ArrayCellInfo / prettyVarName().
    std::map<std::string, ArrayCellInfo> array_cell_info;

    bool integer_mode = false;
    bool is_linearized = false;

    std::string input_file;

    LassoProgram();

    bool hasNoStem() const;
    bool hasNoLoop() const;
    std::string toString() const;

    // Applies rewriting + linearization to raw_formula → populates polyhedra.
    // No-op if already linearized.
    // cancel_flag: forwarded to JsonTraceParser::parseToLasso so the calling
    // technique's own cancellation flag reaches the deeply nested preprocessing.
    // Optional: nullptr (default) means never cancel.
    //
    // Computed at most once and shared across every copy of this object
    // (see m_linearization_cache) instead of each copy redoing the work.
    LassoProgram linearize(const std::atomic<bool>* cancel_flag = nullptr);

    /**
     * Déclare tout le contexte du LassoProgram dans un solveur SMT :
     * constantes, fonctions non interprétées, axiomes,
     * variables SSA (stem + loop), et abstractions de fonctions.
     *
     */
    void declareSolverContext(SMTSolverInterface* solver, bool linearized=false) const;

    /**
     * @brief Readable name for `var`: "A[i]" if it's a promoted array cell
     * (see array_cell_info), else `var` unchanged. Generic over index count,
     * so a future multi-dimensional promotion needs no change here.
     */
    std::string prettyVarName(const std::string& var) const {
        auto it = array_cell_info.find(var);
        if (it == array_cell_info.end()) return var;
        std::ostringstream oss;
        oss << it->second.array_name;
        for (const auto& idx : it->second.index_display) oss << "[" << idx << "]";
        return oss.str();
    }

    /**
     * @brief Mask aligned with program_vars: true at index j iff program_vars[j]
     * has no genuine SSA occurrence on the loop's "in" or "out" side (its
     * mapping was synthesized by the parser's ensureMapping fallback -- see
     * json_trace_parser.cpp -- to keep getSSAVar() from throwing, not read
     * from the loop's real formula).
     *
     * Such a variable carries no information about the reachable state: its
     * "in"/"out" SSA instances are free symbols unconstrained by Loop(x) and
     * unrelated to each other. A ranking/guard template must never be given
     * a nonzero coefficient on one -- Motzkin elimination cannot pin it down
     * (nothing else in the loop's constraints mentions it), so the solver is
     * free to pick whatever value makes an obligation pass without that
     * having any bearing on real program behavior.
     */
    std::vector<bool> loopPhantomVarMask() const {
        std::vector<bool> mask(program_vars.size(), false);
        for (size_t j = 0; j < program_vars.size(); ++j) {
            const auto& var = program_vars[j];
            auto it_in = loop.var_to_ssa_in.find(var);
            auto it_out = loop.var_to_ssa_out.find(var);
            bool in_fresh  = (it_in  == loop.var_to_ssa_in.end())  || (it_in->second.find("_fresh_")  != std::string::npos);
            bool out_fresh = (it_out == loop.var_to_ssa_out.end()) || (it_out->second.find("_fresh_") != std::string::npos);
            mask[j] = in_fresh || out_fresh;
        }
        return mask;
    }

private:
    // Shared, lazily-computed linearize() result -- see linearize().
    std::shared_ptr<LinearizationCache> m_linearization_cache;
};

#endif // __LASSO_PROGRAM_H
