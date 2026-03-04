#ifndef __LASSO_PROGRAM_H
#define __LASSO_PROGRAM_H

#include "transition.h"

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


// Lasso = stem ; loop*
class LassoProgram {
public:
    LinearTransition stem;
    LinearTransition loop;
    std::vector<std::string> program_vars;

    // Variables effectives du loop : intersection loop.var_to_ssa_in ∩ loop.var_to_ssa_out.
    // Matching Ultimate: template variables = loop.getOutVars() ∩ loop.getInVars().
    // Seules ces variables apparaissent dans la RF et les SI.
    std::vector<std::string> loop_vars;

    // Constantes symboliques (ERC20, null, true, false, etc.)
    std::vector<DeclaredConstant> constants;

    // Fonctions non interprétées (sum__balances, DType, etc.)
    std::vector<UninterpretedFunction> functions;

    // Axiomes (forall, injectivité, propriétés de fonctions, etc.)
    std::vector<Axiom> axioms;

    // Abstractions de fonctions créées par la linéarisation
    // Chaque entrée lie une variable fraîche à l'appel de fonction original
    std::vector<FunctionAbstraction> function_abstractions;

    // Sorts des variables de programme (par défaut "Int")
    // Utilisé pour les variables de type Array : {"balance" -> "(Array Int Int)"}
    std::map<std::string, std::string> var_sorts;

    // True if the program contains genuine integer variables (not Bool rewritten to Int).
    // When true, nontermination analyses (geometric, fixpoint) must declare
    // all coefficients as "Int" to ensure soundness over integers.
    // When false (only Bool/Real variables), coefficients can be "Real".
    bool integer_mode = false;

    LassoProgram();

    bool hasNoStem() const;
    bool hasNoLoop() const;
    std::string toString() const;

    /**
     * Déclare tout le contexte du LassoProgram dans un solveur SMT :
     * constantes, fonctions non interprétées, axiomes,
     * variables SSA (stem + loop), et abstractions de fonctions.
     *
     * Centralise la logique utilisée par les analyseurs de terminaison,
     * non-terminaison et le validateur post-synthèse.
     */
    void declareSolverContext(std::shared_ptr<SMTSolver> solver) const;
};

#endif // __LASSO_PROGRAM_H