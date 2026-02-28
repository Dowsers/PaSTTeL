#include <iostream>

#include "lasso_program.h"
#include "utiles.h"

// Constructeur par défaut
LassoProgram::LassoProgram() {
    // stem et loop sont automatiquement initialisés par leurs constructeurs
    // stem commence comme "true" (pas de contraintes)
    // loop commence comme "true" aussi
}

void LassoProgram::declareSolverContext(std::shared_ptr<SMTSolver> solver) const {
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // 1. Déclarer les constantes symboliques
    for (const auto& constant : constants) {
        if (!solver->variableExists(constant.name)) {
            solver->declareVariable(constant.name, constant.type);
            if (verbose) {
                std::cout << "  (declare-const " << constant.name
                          << " " << constant.type << ")";
                if (!constant.value.empty())
                    std::cout << "  ; " << constant.value;
                std::cout << std::endl;
            }
        }
    }

    // 2. Déclarer les fonctions non interprétées
    for (const auto& function : functions) {
        solver->declareFunction(function.name, function.signature);
        if (verbose) {
            std::cout << "  (declare-fun " << function.name
                      << " " << function.signature << ")" << std::endl;
        }
    }

    // 3. Ajouter les axiomes
    for (const auto& axiom : axioms) {
        solver->addAxiom(axiom.formula);
        if (verbose) {
            std::cout << "  (assert " << axiom.formula << ")";
            if (!axiom.description.empty())
                std::cout << "  ; " << axiom.description;
            std::cout << std::endl;
        }
    }

    // 4. Déclarer les variables SSA des transitions (stem + loop)
    // Utilise var_sorts pour déterminer le sort (défaut: "Int")
    // Bool variables are declared as Int (RewriteBooleans converts them to 0/1 integers)
    auto declareTransitionVars = [&](const LinearTransition& trans) {
        for (const auto& [var_prog, ssa_in] : trans.var_to_ssa_in) {
            if (!solver->variableExists(ssa_in)) {
                auto sort_it = var_sorts.find(var_prog);
                std::string sort = (sort_it != var_sorts.end()) ? sort_it->second : "Int";
                if (sort == "Bool") sort = "Int";  // Bool vars are rewritten to Int 0/1
                solver->declareVariable(ssa_in, sort);
            }
        }
        for (const auto& [var_prog, ssa_out] : trans.var_to_ssa_out) {
            if (!solver->variableExists(ssa_out)) {
                auto sort_it = var_sorts.find(var_prog);
                std::string sort = (sort_it != var_sorts.end()) ? sort_it->second : "Int";
                if (sort == "Bool") sort = "Int";  // Bool vars are rewritten to Int 0/1
                solver->declareVariable(ssa_out, sort);
            }
        }
    };

    if (!hasNoStem())
        declareTransitionVars(stem);
    declareTransitionVars(loop);

    // 5. Déclarer les variables d'abstraction de fonctions et ajouter les assertions
    for (const auto& abs : function_abstractions) {
        solver->declareVariable(abs.fresh_var, abs.sort);
        if (!abs.original_call.empty()) {
            // Standard abstraction: equality assertion (= fresh_var original_call)
            std::string assertion = "(= " + abs.fresh_var + " " + abs.original_call + ")";
            solver->addAssertion(assertion);
            if (verbose) {
                std::cout << "  (declare-const " << abs.fresh_var
                          << " " << abs.sort << ")" << std::endl;
                std::cout << "  (assert " << assertion << ")" << std::endl;
            }
        } else if (verbose) {
            // Rewriter aux var: constraints are already conjoined in the formula
            std::cout << "  (declare-const " << abs.fresh_var
                      << " " << abs.sort << ")  ; rewriter aux var" << std::endl;
        }
    }
}


// Vérifie s'il n'y a pas de partie stem
bool LassoProgram::hasNoStem() const {
    return stem.isTrue();
}

// Vérifie s'il n'y a pas de partie loop (aucune transition parsée)
// polyhedra vide (0 polyèdres) = pas de disjonct = aucune transition dans le loop
// NB: isTrue() (1 polyèdre vide) = loop formula "true" = boucle infinie sans garde
bool LassoProgram::hasNoLoop() const {
    return loop.polyhedra.empty();
}

std::string LassoProgram::toString() const {

    std::ostringstream oss;
    oss << "╔═══════════════════════════════════════╗\n";
    oss << "║ LASSO PROGRAM                         ║\n";
    oss << "╚═══════════════════════════════════════╝\n";
    
    // Variables
    oss << "Variables: {";
    for (size_t i = 0; i < program_vars.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << program_vars[i];
    }
    oss << "}\n";
    
    // Stem
    oss << "\n── Stem ──\n";
    if (hasNoStem()) {
        oss << "  (no stem - direct entry to loop)\n";
    } else {
        oss << "  Input vars:  {";
        bool first = true;
        for (const auto& [var_prog, ssa] : stem.var_to_ssa_in) {
            if (!first) oss << ", ";
            oss << var_prog << "=" << ssa;
            first = false;
        }
        oss << "}\n";
        
        oss << "  Output vars: {";
        first = true;
        for (const auto& [var_prog, ssa] : stem.var_to_ssa_out) {
            if (!first) oss << ", ";
            oss << var_prog << "=" << ssa;
            first = false;
        }
        oss << "}\n";
        
        oss << "  Transition:  " << stem.toString() << "\n";
        oss << "  SMT-lib2:    " << stem.toSMTLib2() << "\n";
    }
    
    // Loop
    oss << "\n── Loop ──\n";
    oss << "  Input vars:  {";
    bool first = true;
    for (const auto& [var_prog, ssa] : loop.var_to_ssa_in) {
        if (!first) oss << ", ";
        oss << var_prog << "=" << ssa;
        first = false;
    }
    oss << "}\n";
    
    oss << "  Output vars: {";
    first = true;
    for (const auto& [var_prog, ssa] : loop.var_to_ssa_out) {
        if (!first) oss << ", ";
        oss << var_prog << "=" << ssa;
        first = false;
    }
    oss << "}\n";
    
    oss << "  Transition:  " << loop.toString() << "\n";
    oss << "  SMT-lib2:    " << loop.toSMTLib2() << "\n";

    oss << "╚═══════════════════════════════════════╝\n";
    
    return oss.str();
}