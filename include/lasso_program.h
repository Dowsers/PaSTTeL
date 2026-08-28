#ifndef __LASSO_PROGRAM_H
#define __LASSO_PROGRAM_H

#include <sstream>

#include "transition.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "utiles.h"


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
    LassoProgram linearize();

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
};

#endif // __LASSO_PROGRAM_H