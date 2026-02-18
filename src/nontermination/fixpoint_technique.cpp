#include <iostream>
#include <sstream>
#include <cmath>
#include <set>

#include "nontermination/fixpoint_technique.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

FixpointTechnique::FixpointTechnique()
    : lasso_(nullptr)
    , initialized_(false) {
}

// ============================================================================
// INITIALISATION
// ============================================================================

void FixpointTechnique::init(const LassoProgram& lasso) {
    lasso_ = &lasso;
    initialized_ = true;
}

// ============================================================================
// VÉRIFICATION PRINCIPALE
// ============================================================================


NonTerminationResult FixpointTechnique::analyze(
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    
    if (!initialized_ || !lasso_) {
        return NonTerminationResult(NonTerminationResult::Type::UNKNOWN, false,
                    "Technique not initialized");
    }

    NonTerminationResult result;
    
    if (verbose){
        std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║     FIXPOINT CHECKER (Non-termination Analysis)      ║" << std::endl;
        std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    }

    // Créer un contexte SMT propre
    solver->push();
    
    // Étape 1 : Ajouter contraintes du stem (si présent)
    if (!lasso_->hasNoStem()) {
        if (verbose)
            std::cout << "\n[1/3] Ajout des contraintes du stem..." << std::endl;
        addStemConstraints(solver);
    } else {
        if (verbose)
            std::cout << "\n[1/3] Pas de stem - analyse directe de la boucle" << std::endl;
    }
    
    // Étape 2 : Ajouter contraintes de la boucle
    if (verbose)
        std::cout << "\n[2/3] Ajout des contraintes de la boucle..." << std::endl;
    addLoopConstraints(solver);
    
    // Étape 3 : Ajouter contraintes de point fixe (x = x')
    if (verbose)
        std::cout << "\n[3/3] Ajout des contraintes de point fixe (x = x')..." << std::endl;
    addFixpointConstraints(solver);
    
    // Vérifier satisfiabilité
    if (verbose)
        std::cout << "\n  • Vérification SAT..." << std::endl;
    bool sat = solver->checkSat();

    if (sat) {
        if (verbose)
            std::cout << "    SAT - Point fixe trouvé!" << std::endl;
        result.is_nonterminating = true;
        result.witness_state = extractFixpoint(solver);
        result.description = "Fixpoint found:  Infinite loop with fixpoint state";
        result.type = NonTerminationResult::Type::FIXPOINT;
        if (verbose){
            std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║  !  NON-TERMINATION PROVED (Fixpoint)              ║" << std::endl;
            std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
        }
    } else {
        if (verbose)
            std::cout << "    UNSAT - Pas de point fixe" << std::endl;
        result.is_nonterminating = false;
        result.description = "No fixpoint exists";
        result.type = NonTerminationResult::Type::UNKNOWN;
        if (verbose)
            std::cout << "\n    Aucun point fixe trouvé (ne prouve pas la terminaison)" << std::endl;
    }

    solver->pop();

    return result;
}

// ============================================================================
// AJOUT DES CONTRAINTES
// ============================================================================

void FixpointTechnique::addStemConstraints(std::shared_ptr<SMTSolver> solver)
{
    std::set<std::string> all_vars;

    // Déclarer les variables du stem
    for (const auto& [var_prog, ssa_in] : lasso_->stem.var_to_ssa_in) {
        all_vars.insert(ssa_in);
        if (!solver->variableExists(ssa_in)) {
            solver->declareVariable(ssa_in, "Int");
        }
    }
    
    for (const auto& [var_prog, ssa_out] : lasso_->stem.var_to_ssa_out) {
        all_vars.insert(ssa_out);
        if (!solver->variableExists(ssa_out)) {
            solver->declareVariable(ssa_out, "Int");
        }
    }
    
    int constraint_count = 0;

    // Collecter les contraintes par polyèdre (DNF)
    std::vector<std::vector<std::string>> poly_constraints;

    for (const auto& poly : lasso_->stem.polyhedra) {
        std::vector<std::string> clause_constraints;

        for (const auto& ineq : poly) {
            bool has_terms = false;

            std::ostringstream lhs;
            lhs << "(+";

            // Itérer sur TOUS les coefficients de l'inégalité (y compris les variables auxiliaires)
            for (const auto& [var, coef] : ineq.coefficients) {
                if (coef.isConstant() && std::abs(coef.constant) > 1e-9) {
                    // Déclarer la variable si pas encore connue (variable auxiliaire)
                    if (!solver->variableExists(var)) {
                        solver->declareVariable(var, "Int");
                    }
                    lhs << " (* " << formatNumber(coef.constant) << " " << var << ")";
                    has_terms = true;
                }
            }

            if (ineq.constant.isConstant()) {
                lhs << " " << formatNumber(ineq.constant.constant);
                has_terms = true;
            }

            lhs << ")";

            if (has_terms) {
                std::ostringstream constraint;
                constraint << "(" << (ineq.strict ? ">" : ">=") << " " << lhs.str() << " 0)";
                clause_constraints.push_back(constraint.str());
            }
        }

        if (!clause_constraints.empty())
            poly_constraints.push_back(clause_constraints);
    }

    // Émettre en DNF
    if (poly_constraints.size() == 1) {
        for (const auto& c : poly_constraints[0]) {
            solver->addAssertion(c);
            constraint_count++;
        }
    } else if (poly_constraints.size() > 1) {
        std::ostringstream dnf;
        dnf << "(or";
        for (const auto& clause : poly_constraints) {
            dnf << " (and";
            for (const auto& c : clause) {
                dnf << " " << c;
            }
            dnf << ")";
        }
        dnf << ")";
        solver->addAssertion(dnf.str());
        constraint_count++;
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "    Ajouté " << constraint_count << " contraintes du stem" << std::endl;
}

void FixpointTechnique::addLoopConstraints(std::shared_ptr<SMTSolver> solver)
{
    // Déclarer les variables de la boucle
    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
        if (!solver->variableExists(ssa_in)) {
            solver->declareVariable(ssa_in, "Int");
        }
    }
    
    for (const auto& [var_prog, ssa_out] : lasso_->loop.var_to_ssa_out) {
        if (!solver->variableExists(ssa_out)) {
            solver->declareVariable(ssa_out, "Int");
        }
    }
    
    int constraint_count = 0;

    // Collecter les contraintes par polyèdre (DNF)
    std::vector<std::vector<std::string>> poly_constraints;

    for (const auto& poly : lasso_->loop.polyhedra) {
        std::vector<std::string> clause_constraints;

        std::ostringstream lhs;
        bool has_terms = false;

        for (const auto& ineq : poly) {
            lhs.str(""); lhs.clear();
            lhs << "(+";
            has_terms = false;

            // Itérer sur TOUS les coefficients (y compris variables auxiliaires)
            for (const auto& [var, coef] : ineq.coefficients) {
                if (coef.isConstant() && std::abs(coef.constant) > 1e-9) {
                    if (!solver->variableExists(var)) {
                        solver->declareVariable(var, "Int");
                    }
                    lhs << " (* " << formatNumber(coef.constant) << " " << var << ")";
                    has_terms = true;
                }
            }

            // Constante
            if (ineq.constant.isConstant()) {
                lhs << " " << formatNumber(ineq.constant.constant);
                has_terms = true;
            }

            lhs << ")";

            if (has_terms) {
                std::ostringstream constraint;
                constraint << "(" << (ineq.strict ? ">" : ">=") << " " << lhs.str() << " 0)";
                clause_constraints.push_back(constraint.str());
            }
        }

        if (!clause_constraints.empty())
            poly_constraints.push_back(clause_constraints);
    }

    // Émettre en DNF
    if (poly_constraints.size() == 1) {
        for (const auto& c : poly_constraints[0]) {
            solver->addAssertion(c);
            constraint_count++;
        }
    } else if (poly_constraints.size() > 1) {
        std::ostringstream dnf;
        dnf << "(or";
        for (const auto& clause : poly_constraints) {
            dnf << " (and";
            for (const auto& c : clause) {
                dnf << " " << c;
            }
            dnf << ")";
        }
        dnf << ")";
        solver->addAssertion(dnf.str());
        constraint_count++;
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "    Ajouté " << constraint_count << " contraintes de la boucle" << std::endl;
}

void FixpointTechnique::addFixpointConstraints(std::shared_ptr<SMTSolver> solver)
{
    // Pour chaque variable du programme, ajouter : in_var = out_var
    int constraint_count = 0;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
        std::string ssa_out = lasso_->loop.getSSAVar(var_prog, true);
        std::ostringstream constraint;
        constraint << "(= " << ssa_in
                << " " << ssa_out << ")";
        solver->addAssertion(constraint.str());
        constraint_count++;
        
        if (verbose)
            std::cout << "    • " << ssa_in
                    << " = " << ssa_out << std::endl;
    }
    
    if (verbose)
        std::cout << "    ✓ Ajouté " << constraint_count << " contraintes de point fixe" << std::endl;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::map<std::string, double> FixpointTechnique::extractFixpoint(
    std::shared_ptr<SMTSolver> solver)
{
    std::map<std::string, double> fixpoint;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    
    if (verbose)
        std::cout << "\n  Point fixe trouvé:" << std::endl;
    
    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
        double value = solver->getValue(ssa_in);
        fixpoint[ssa_in] = value;
        if (verbose)
            std::cout << "    • " << ssa_in << " = " << value << std::endl;
    }
    
    return fixpoint;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void FixpointTechnique::printResult(const NonTerminationResult& result) const {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║    FIXPOINT ANALYSIS RESULT                         ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    
    if (result.is_nonterminating) {
        std::cout << "\n  !  NON-TERMINATING: Fixpoint detected" << std::endl;
        std::cout << "  " << result.description << std::endl;
        
        std::cout << "\n  Fixpoint state:" << std::endl;
        for (const auto& [var, value] : result.witness_state) {
            std::cout << "    • " << var << " = " << value << std::endl;
        }
        
        std::cout << "\n  The program loops infinitely at this state." << std::endl;
    } else {
        std::cout << "\n  ✓ No fixpoint found" << std::endl;
        std::cout << "  " << result.description << std::endl;
        std::cout << "\n  (This does not prove termination)" << std::endl;
    }
}

// ============================================================================
// VALIDATION
// ============================================================================

bool FixpointTechnique::validateConfiguration() const {
    return initialized_ && lasso_ != nullptr;
}
