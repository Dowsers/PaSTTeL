#ifndef __LASSO_PROGRAM_H
#define __LASSO_PROGRAM_H

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

// Lasso = stem ; loop*
class LassoProgram {
public:
    LinearTransition stem;
    LinearTransition loop;
    std::vector<std::string> program_vars;

    // Variables effectives du loop
    std::vector<std::string> loop_vars;

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
     */
    void declareSolverContext(std::shared_ptr<SMTSolver> solver) const;
};

#endif // __LASSO_PROGRAM_H