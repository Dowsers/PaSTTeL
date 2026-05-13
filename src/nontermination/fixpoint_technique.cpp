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

FixpointTechnique::FixpointTechnique(SMTSolverInterface* solver)
    : lasso_(nullptr)
    , initialized_(false) {
    solver_ = solver;
}

// ============================================================================
// INITIALISATION
// ============================================================================

void FixpointTechnique::init(const LassoProgram& lasso) {
    lasso_ = &lasso;
    initialized_ = true;
    lasso.declareSolverContext(solver_, false);
}

// ============================================================================
// VÉRIFICATION PRINCIPALE
// ============================================================================


AnalysisResult FixpointTechnique::analyze() {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    ProofCertificate proof;
    proof.technique_name = getName();

    if (!initialized_ || !lasso_) {
        proof.description = "Technique not initialized";
        proof_ = proof;
        return proof_.status;
    }

    if (verbose){
        std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║     FIXPOINT CHECKER (Non-termination Analysis)      ║" << std::endl;
        std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    }

    // Cas trivial : loop = "true" → boucle infinie sans contrainte
    // Only valid when using linearized polyhedra (raw_formula non-empty means real constraints exist)
    if (lasso_->loop.raw_formula.empty() && lasso_->loop.isTrue()) {
        if (verbose)
            std::cout << "\n  Loop is 'true' → trivial fixpoint (any state loops forever)" << std::endl;
        proof.status = AnalysisResult::NON_TERMINATING;
        proof.description = "Loop guard is 'true': the loop runs forever unconditionally.";
        proof_ = proof;
        return proof_.status;
    }

    solver_->push();

    if (!lasso_->hasNoStem()) {
        if (verbose)
            std::cout << "\n[1/3] Ajout des contraintes du stem..." << std::endl;
        addStemConstraints();
    } else {
        if (verbose)
            std::cout << "\n[1/3] Pas de stem - analyse directe de la boucle" << std::endl;
    }

    if (verbose)
        std::cout << "\n[2/3] Ajout des contraintes de la boucle..." << std::endl;
    addLoopConstraints();

    if (verbose)
        std::cout << "\n[3/3] Ajout des contraintes de point fixe (x = x')..." << std::endl;
    addFixpointConstraints();

    if (verbose)
        std::cout << "\n  • Vérification SAT..." << std::endl;
    bool sat = solver_->checkSat();

    if (sat) {
        if (verbose)
            std::cout << "    SAT - Point fixe trouvé!" << std::endl;
        proof.status = AnalysisResult::NON_TERMINATING;
        proof.nt_witness_state = extractFixpoint();
        proof.description = "Fixpoint found: Infinite loop with fixpoint state";
        if (verbose){
            std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║  !  NON-TERMINATION PROVED (Fixpoint)              ║" << std::endl;
            std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
        }
    } else {
        if (verbose)
            std::cout << "    UNSAT - Pas de point fixe" << std::endl;
        proof.description = "No fixpoint exists";
        if (verbose)
            std::cout << "\n    Aucun point fixe trouvé (ne prouve pas la terminaison)" << std::endl;
    }

    solver_->pop();

    proof_ = proof;
    return proof_.status;
}

// ============================================================================
// AJOUT DES CONTRAINTES
// ============================================================================

void FixpointTechnique::addStemConstraints()
{
    // Use raw formula directly when available (avoids linearization blowup).
    if (!lasso_->stem.raw_formula.empty()) {
        solver_->addAssertion(lasso_->stem.raw_formula);
        if (VERBOSITY == VerbosityLevel::VERBOSE)
            std::cout << "    Stem: raw formula asserted" << std::endl;
        return;
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
                    if (!solver_->variableExists(var)) {
                        solver_->declareVariable(var, "Int");
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
            solver_->addAssertion(c);
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
        solver_->addAssertion(dnf.str());
        constraint_count++;
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "    Ajouté " << constraint_count << " contraintes du stem" << std::endl;
}

void FixpointTechnique::addLoopConstraints()
{
    // Use raw formula directly when available (avoids linearization blowup).
    if (!lasso_->loop.raw_formula.empty()) {
        solver_->addAssertion(lasso_->loop.raw_formula);
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "    Loop: raw formula asserted" << std::endl;
            std::cout << "    Loop formula: " << lasso_->loop.raw_formula << std::endl;
        }
        return;
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
                    if (!solver_->variableExists(var)) {
                        solver_->declareVariable(var, "Int");
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
            solver_->addAssertion(c);
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
        solver_->addAssertion(dnf.str());
        constraint_count++;
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "    Ajouté " << constraint_count << " contraintes de la boucle" << std::endl;
}

void FixpointTechnique::addFixpointConstraints()
{
    // Pour chaque variable du programme, ajouter : in_var = out_var.
    // On ignore :
    //   - les paires triviales ssa_in == ssa_out (toujours vraies, inutiles)
    //   - les paires où au moins une var est fraîche (absente de la formule du loop)
    //     car elles permettent un modèle trivial non-représentatif
    //   - les variables de sort Array (non-scalaires)
    int constraint_count = 0;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
        std::string ssa_out = lasso_->loop.getSSAVar(var_prog, true);

        // Ignorer les contraintes triviales (variable inchangée)
        if (ssa_in == ssa_out) continue;

        // Ignorer si l'une ou l'autre est une variable fraîche
        if (ssa_in.find("_fresh_")  != std::string::npos ||
            ssa_out.find("_fresh_") != std::string::npos)
            continue;

        // Ignorer les variables Array
        auto sort_it = lasso_->var_sorts.find(var_prog);
        if (sort_it != lasso_->var_sorts.end() &&
            sort_it->second.find("Array") != std::string::npos)
            continue;

        std::ostringstream constraint;
        constraint << "(= " << ssa_in << " " << ssa_out << ")";
        solver_->addAssertion(constraint.str());
        constraint_count++;

        if (verbose)
            std::cout << "    • " << ssa_in << " = " << ssa_out << std::endl;
    }

    if (verbose)
        std::cout << "    ✓ Ajouté " << constraint_count << " contraintes de point fixe" << std::endl;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::map<std::string, double> FixpointTechnique::extractFixpoint()
{
    std::map<std::string, double> fixpoint;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    
    if (verbose)
        std::cout << "\n  Point fixe trouvé:" << std::endl;
    
    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
        auto it = lasso_->var_sorts.find(var_prog);
        if (it != lasso_->var_sorts.end() && it->second.find("Array") != std::string::npos)
            continue;
        double value = solver_->getValue(ssa_in);
        fixpoint[ssa_in] = value;
        if (verbose)
            std::cout << "    • " << ssa_in << " = " << value << std::endl;
    }
    
    return fixpoint;
}


// ============================================================================
// VALIDATION
// ============================================================================

bool FixpointTechnique::validateConfiguration() const {
    return initialized_ && lasso_ != nullptr;
}
