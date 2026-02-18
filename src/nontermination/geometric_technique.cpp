#include <iostream>
#include <sstream>
#include <cmath>
#include <set>

#include "nontermination/geometric_technique.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;



// ============================================================================
// CONSTRUCTEUR
// ============================================================================

GeometricTechnique::GeometricTechnique(const GeometricNonTerminationSettings& settings)
    : settings_(settings)
    , lasso_(nullptr)
    , initialized_(false) {
}

// ============================================================================
// INITIALISATION
// ============================================================================

void GeometricTechnique::init(const LassoProgram& lasso) {
    lasso_ = &lasso;
    initialized_ = true;
    // Nettoyer les résultats précédents
    state_init.clear();
    state_honda.clear();
    eigenvectors.clear();
    lambdas.clear();
    nus.clear();
}

// ============================================================================
// SYNTHÈSE PRINCIPALE
//
// Recherche géométrique avec n GEVs, composantes nilpotentes,
//
// Le fixpoint (GEV=0) est géré séparément par FixpointTechnique.
// ============================================================================

NonTerminationResult GeometricTechnique::analyze(
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (!initialized_ || !lasso_) {
        return NonTerminationResult(NonTerminationResult::Type::UNKNOWN, false,
                    "Technique not initialized");
    }

    if (verbose) {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║         GEOMETRIC NONTERMINATION SYNTHESIZER          ║" << std::endl;
        std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;

        std::cout << "\n  Settings:" << std::endl;
        std::cout << "    • Number of GEVs: " << settings_.num_gevs << std::endl;
        std::cout << "    • Allow bounded: " << (settings_.allow_bounded ? "yes" : "no") << std::endl;
        std::cout << "    • Nilpotent components: " << (settings_.nilpotent_components ? "yes" : "no") << std::endl;
    }

    // ========================================================================
    // Recherche géométrique avec n GEVs complets
    // Contraintes:
    //   1. Stem(x₀, x₁)
    //   2. Loop(x₁, x₁ + y₁ + ... + yₙ)
    //   3. Loop(yᵢ, λᵢ·yᵢ + νᵢ·yᵢ₊₁) pour chaque i
    //   4. Bornes sur λᵢ et νᵢ
    // ========================================================================

    solver->push();

    if (verbose)
        std::cout << "\n[1/4] Declaring SMT variables..." << std::endl;
    declareVariables(solver, settings_.num_gevs);

    if (verbose)
        std::cout << "\n[2/4] Encoding constraints..." << std::endl;
    encodeConstraints(solver, settings_.num_gevs);

    if (verbose)
        std::cout << "\n[3/4] Checking satisfiability..." << std::endl;
    bool sat = solver->checkSat();

    NonTerminationResult result;

    if (sat) {
        if (verbose)
            std::cout << "    SAT - Geometric nontermination argument found!" << std::endl;

        if (verbose)
            std::cout << "\n[4/4] Extracting GNTA..." << std::endl;
        result = extractGNTA(solver, settings_.num_gevs);

        if (verbose) {
            std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║  NON-TERMINATION PROVED (Geometric)                  ║" << std::endl;
            std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
        }
    } else {
        if (verbose)
            std::cout << "    UNSAT - No geometric nontermination argument found" << std::endl;
        result.is_nonterminating = false;
        result.description = "No geometric nontermination argument found";
        result.type = NonTerminationResult::Type::UNKNOWN;
    }

    solver->pop();

    return result;
}

// ============================================================================
// GeometricTechnique - Méthodes utilitaires
// ============================================================================

bool GeometricTechnique::isFixpoint() const {

    // Fixpoint si tous les eigenvectors sont nuls ou tous les lambdas sont nuls
    bool all_gevs_zero = true;
    for (const auto& gev : eigenvectors) {
        for (const auto& [var, val] : gev) {
            if (std::abs(val) > 1e-9) {
                all_gevs_zero = false;
                break;
            }
        }
        if (!all_gevs_zero) break;
    }

    bool all_lambdas_zero = true;
    for (double lambda : lambdas) {
        if (std::abs(lambda) > 1e-9) {
            all_lambdas_zero = false;
            break;
        }
    }

    return all_gevs_zero || all_lambdas_zero;
}

// ============================================================================
// DÉCLARATION DES VARIABLES
// ============================================================================
void GeometricTechnique::declareVariables(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Sort homogène : "Int" si le programme contient des entiers, "Real" sinon
    const std::string sort = lasso_->integer_mode ? "Int" : "Real";

    if (verbose) {
        std::cout << "    Variable sort: " << sort
                  << " (integer_mode=" << (lasso_->integer_mode ? "true" : "false") << ")" << std::endl;
    }

    // Variables d'état initial (x₀)
    for (const auto& var : lasso_->program_vars) {
        std::string init_var = "x0_" + var;
        if (!solver->variableExists(init_var)) {
            solver->declareVariable(init_var, sort);
        }
    }

    // Variables d'état honda (x₁)
    for (const auto& var : lasso_->program_vars) {
        std::string honda_var = "x1_" + var;
        if (!solver->variableExists(honda_var)) {
            solver->declareVariable(honda_var, sort);
        }
    }

    // Eigenvectors (yᵢ) pour i = 0..n-1
    for (int i = 0; i < effective_num_gevs; ++i) {
        for (const auto& var : lasso_->program_vars) {
            std::string gev_var = "v" + std::to_string(i) + "_" + var;
            if (!solver->variableExists(gev_var)) {
                solver->declareVariable(gev_var, sort);
            }
        }
    }

    // Eigenvalues (λᵢ) pour i = 0..n-1
    for (int i = 0; i < effective_num_gevs; ++i) {
        std::string lambda_var = "lambda_" + std::to_string(i);
        if (!solver->variableExists(lambda_var)) {
            solver->declareVariable(lambda_var, sort);
        }
    }

    // Composantes nilpotentes (νᵢ) pour i = 0..n-2
    // mGEVs.size() == nus.size() + 1
    if (settings_.nilpotent_components && effective_num_gevs >= 2) {
        for (int i = 0; i < effective_num_gevs - 1; ++i) {
            std::string nu_var = "nu_" + std::to_string(i);
            if (!solver->variableExists(nu_var)) {
                solver->declareVariable(nu_var, sort);
            }
        }
    }

    if (verbose) {
        std::cout << "    Declared variables for " << lasso_->program_vars.size()
                << " program variables" << std::endl;
        std::cout << "    Declared " << effective_num_gevs << " eigenvector(s) and eigenvalue(s)" << std::endl;
        if (settings_.nilpotent_components && effective_num_gevs >= 2) {
            std::cout << "    Declared " << (effective_num_gevs - 1) << " nilpotent component(s)" << std::endl;
        }
    }
}

// ============================================================================
// ENCODAGE DES CONTRAINTES 
// ============================================================================

bool GeometricTechnique::encodeConstraints(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // 1. Contraintes du stem (si présent)
    if (!lasso_->hasNoStem()) {
        if (verbose)
            std::cout << "    • Adding stem constraints: Stem(x0, x1)" << std::endl;
        addStemConstraints(solver);
    } else {
        if (verbose)
            std::cout << "    • No stem - direct loop analysis" << std::endl;
    }

    // 2. Première itération: Loop(x₁, x₁ + y₁ + ... + yₙ)
    if (verbose)
        std::cout << "    • Adding first iteration: Loop(x1, x1 + y1 + ... + y"
                  << effective_num_gevs << ")" << std::endl;
    addFirstIterationConstraints(solver, effective_num_gevs);

    // 3. Contraintes rayon pour chaque GEV
    if (verbose)
        std::cout << "    • Adding ray constraints for " << effective_num_gevs << " GEV(s)" << std::endl;
    addRayConstraints(solver, effective_num_gevs);

    // 4. Contraintes d'identité pour variables inchangées (in_ssa == out_ssa)
    if (verbose)
        std::cout << "    • Adding identity variable constraints" << std::endl;
    addIdentityVariableConstraints(solver, effective_num_gevs);

    // 5. Contraintes sur eigenvalues et nilpotent
    if (verbose)
        std::cout << "    • Adding eigenvalue and nilpotent constraints" << std::endl;
    addEigenvalueAndNilpotentConstraints(solver, effective_num_gevs);

    return true;
}

// ============================================================================
// CONTRAINTES DU STEM: Stem(x₀, x₁)
// ============================================================================

void GeometricTechnique::addStemConstraints(
    std::shared_ptr<SMTSolver> solver)
{
    int constraint_count = 0;

    // Encoder chaque polyèdre comme une conjonction, puis les combiner en DNF
    std::vector<std::vector<std::string>> poly_constraints;
    std::vector<std::string> identity_assertions; // Pour les variables inchangées : x0_var == x1_var

    for (const auto& poly : lasso_->stem.polyhedra) {
        std::vector<std::string> clause_constraints;

        for (const auto& ineq : poly) {
            std::ostringstream lhs;
            lhs << "(+";
            bool has_terms = false;

            for (const auto& [var, coef] : ineq.coefficients) {
                if (coef.isConstant() && std::abs(coef.constant) > 1e-9) {
                    // Chercher si c'est une variable in/out du stem
                    bool is_input = false;
                    bool is_output = false;
                    std::string prog_var;
                    // TODO: simplify this
                    for (const auto& [vp, ssa_in] : lasso_->stem.var_to_ssa_in) {
                        if (ssa_in == var) { is_input = true; prog_var = vp; break; }
                    }
                    for (const auto& [vp, ssa_out] : lasso_->stem.var_to_ssa_out) {
                        if (ssa_out == var) { is_output = true; prog_var = vp; break; }
                    }

                    if (is_input && is_output) {
                        std::string smt_var = is_input ? "x0_" + prog_var : "x1_" + prog_var;
                        lhs << " (* " << formatNumber(coef.constant) << " " << smt_var << ")";
                        has_terms = true;
                        identity_assertions.push_back("(= x0_" + prog_var + " x1_" + prog_var + ")");
                    } else if (is_output) {
                        lhs << " (* " << coef.constant << " x1_" << prog_var << ")";
                        has_terms = true;
                    } else if (is_input) {
                        lhs << " (* " << coef.constant << " x0_" << prog_var << ")";
                        has_terms = true;
                    } else {
                        // Variable auxiliaire : déclarer comme variable SMT libre
                        if (!solver->variableExists(var)) {
                            solver->declareVariable(var, "Int");
                        }
                        lhs << " (* " << formatNumber(coef.constant) << " " << var << ")";
                        has_terms = true;
                    }
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

    // Émettre en DNF: si un seul polyèdre, conjonction directe; sinon (or (and ...) (and ...))
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
    
    for(auto & id_assertion : identity_assertions) {
        solver->addAssertion(id_assertion);
        constraint_count++;
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "      Added " << constraint_count << " stem constraints" << std::endl;
}

// ============================================================================
// PREMIÈRE ITÉRATION: Loop(x₁, x₁ + y₁ + ... + yₙ) avec rays=FALSE
//
//   A_loop * (x₁, x₁ + y₁ + ... + yₙ) <= b_loop
// ============================================================================

void GeometricTechnique::addFirstIterationConstraints(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    int constraint_count = 0;

    std::set<std::string> all_vars;
    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in)
        all_vars.insert(ssa_in);
    for (const auto& [var_prog, ssa_out] : lasso_->loop.var_to_ssa_out)
        all_vars.insert(ssa_out);

    // Collecter les contraintes par polyèdre (DNF)
    std::vector<std::vector<std::string>> poly_constraints;

    for (const auto& poly : lasso_->loop.polyhedra) {
        std::vector<std::string> clause_constraints;

        for (const auto& ineq : poly) {
            std::ostringstream lhs;
            lhs << "(+";
            bool has_terms = false;

            // TODO: improve this loop
            for (const auto& var : all_vars) {
                AffineTerm coef = ineq.getCoefficient(var);
                if (coef.isConstant() && std::abs(coef.constant) > 1e-9) {

                    bool is_input = false;
                    bool is_output = false;
                    std::string prog_var;
                    for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
                        if (ssa_in == var) { is_input = true; prog_var = var_prog; break; }
                    }
                    for (const auto& [var_prog, ssa_out] : lasso_->loop.var_to_ssa_out) {
                        if (ssa_out == var) { is_output = true; prog_var = var_prog; break; }
                    }

                    if (is_input && is_output) {
                        // Identity variable (x_in = x_out, same SSA)
                        // Treat as INPUT only; identity constraints added separately
                        lhs << " (* " << formatNumber(coef.constant) << " x1_" << prog_var << ")";
                        has_terms = true;
                    } else if (is_output) {
                        // out_var → x₁ + y₁ + ... + yₙ
                        std::ostringstream sum;
                        sum << "(+ x1_" << prog_var;
                        for (int i = 0; i < effective_num_gevs; ++i) {
                            sum << " v" << i << "_" << prog_var;
                        }
                        sum << ")";
                        lhs << " (* " << formatNumber(coef.constant) << " " << sum.str() << ")";
                        has_terms = true;
                    } else if (is_input) {
                        // in_var → x₁
                        lhs << " (* " << coef.constant << " x1_" << prog_var << ")";
                        has_terms = true;
                    }
                }
            }

            // RAYS=FALSE : Garder la constante
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
        std::cout << "      Added " << constraint_count << " first iteration constraints" << std::endl;
}

// ============================================================================
// CONTRAINTES RAYON POUR CHAQUE GEV
//
// Pour chaque GEV i (0..n-1):
//   Loop(yᵢ, λᵢ·yᵢ + νᵢ·yᵢ₊₁) avec rays=true (constante=0)
//
// Pour le dernier GEV (i = n-1), pas de composante nilpotente:
//   Loop(yₙ₋₁, λₙ₋₁·yₙ₋₁)
//
// ============================================================================

void GeometricTechnique::addRayConstraints(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    int constraint_count = 0;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    for (int gev_idx = 0; gev_idx < effective_num_gevs; ++gev_idx) {

        bool has_next_gev = (gev_idx < effective_num_gevs - 1);
        bool use_nilpotent = settings_.nilpotent_components && has_next_gev;

        if (verbose) {
            if (use_nilpotent)
                std::cout << "      GEV " << gev_idx << ": Loop(y" << gev_idx
                          << ", lambda_" << gev_idx << "*y" << gev_idx
                          << " + nu_" << gev_idx << "*y" << (gev_idx + 1) << ")" << std::endl;
            else
                std::cout << "      GEV " << gev_idx << ": Loop(y" << gev_idx
                          << ", lambda_" << gev_idx << "*y" << gev_idx << ")" << std::endl;
        }

        std::set<std::string> all_vars;
        for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in)
            all_vars.insert(ssa_in);
        for (const auto& [var_prog, ssa_out] : lasso_->loop.var_to_ssa_out)
            all_vars.insert(ssa_out);

        // Collecter les contraintes par polyèdre (DNF)
        std::vector<std::vector<std::string>> poly_constraints;

        for (const auto& poly : lasso_->loop.polyhedra) {
            std::vector<std::string> clause_constraints;

            for (const auto& ineq : poly) {
                std::ostringstream lhs;
                lhs << "(+";
                bool has_terms = false;

                for (const auto& var : all_vars) {
                    AffineTerm coef = ineq.getCoefficient(var);
                    if (coef.isConstant() && std::abs(coef.constant) > 1e-9) {
                        bool is_input = false;
                        bool is_output = false;
                        std::string prog_var;
                        for (const auto& [var_prog, ssa_in] : lasso_->loop.var_to_ssa_in) {
                            if (ssa_in == var) { is_input = true; prog_var = var_prog; break; }
                        }
                        for (const auto& [var_prog, ssa_out] : lasso_->loop.var_to_ssa_out) {
                            if (ssa_out == var) { is_output = true; prog_var = var_prog; break; }
                        }

                        if (is_input && is_output) {
                            // Identity variable (x_in = x_out, same SSA)
                            // Treat as INPUT only; identity constraints added separately
                            std::string gev_var = "v" + std::to_string(gev_idx) + "_" + prog_var;
                            lhs << " (* " << formatNumber(coef.constant) << " " << gev_var << ")";
                            has_terms = true;
                        } else if (is_output) {
                            // out_var → λᵢ·yᵢ + νᵢ·yᵢ₊₁
                            std::string gev_var = "v" + std::to_string(gev_idx) + "_" + prog_var;
                            std::string lambda_var = "lambda_" + std::to_string(gev_idx);

                            if (use_nilpotent) {
                                std::string nu_var = "nu_" + std::to_string(gev_idx);
                                std::string next_gev_var = "v" + std::to_string(gev_idx + 1) + "_" + prog_var;
                                lhs << " (* " << coef.constant
                                    << " (+ (* " << lambda_var << " " << gev_var << ")"
                                    << " (* " << nu_var << " " << next_gev_var << ")))";
                            } else {
                                lhs << " (* " << coef.constant
                                    << " (* " << lambda_var << " " << gev_var << "))";
                            }
                            has_terms = true;
                        } else if (is_input) {
                            std::string gev_var = "v" + std::to_string(gev_idx) + "_" + prog_var;
                            lhs << " (* " << formatNumber(coef.constant) << " " << gev_var << ")";
                            has_terms = true;
                        }
                    }
                }

                // RAYS=TRUE : PAS de constante (homogène)

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
    }

    if (verbose)
        std::cout << "      Added " << constraint_count << " ray constraints total" << std::endl;
}

// ============================================================================
// CONTRAINTES D'IDENTITÉ POUR VARIABLES INCHANGÉES
//
// Pour les variables où in_ssa == out_ssa (la boucle ne modifie pas la variable),
// on ajoute des contraintes explicites :
//
// 1. Première itération : Σᵢ vᵢ_x = 0
//    (la somme des composantes eigenvector pour x doit être nulle)
//
// 2. Pour chaque GEV i : vᵢ_x = λᵢ·vᵢ_x + νᵢ·vᵢ₊₁_x
//    (le rayon doit respecter l'identité x' = x)
//
// Sans ces contraintes, le solveur peut trouver des preuves spurieuses
// où une variable inchangée croît via les eigenvectors.
// ============================================================================

void GeometricTechnique::addIdentityVariableConstraints(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    int constraint_count = 0;

    // Collect identity variables: in_ssa == out_ssa
    std::vector<std::string> identity_vars;
    for (const auto& var : lasso_->program_vars) {
        auto it_in = lasso_->loop.var_to_ssa_in.find(var);
        auto it_out = lasso_->loop.var_to_ssa_out.find(var);
        if (it_in != lasso_->loop.var_to_ssa_in.end() &&
            it_out != lasso_->loop.var_to_ssa_out.end() &&
            it_in->second == it_out->second) {
            identity_vars.push_back(var);
        }
    }

    if (identity_vars.empty()) {
        if (verbose)
            std::cout << "      No identity variables found" << std::endl;
        return;
    }

    if (verbose) {
        std::cout << "      Identity variables (unchanged by loop): ";
        for (size_t i = 0; i < identity_vars.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << identity_vars[i];
        }
        std::cout << std::endl;
    }

    // Literal adapté au sort
    const std::string zero = lasso_->integer_mode ? "0" : "0.0";

    // 1. First iteration identity: sum(v_i_x) = 0 for each identity var
    for (const auto& var : identity_vars) {
        std::ostringstream sum;
        if (effective_num_gevs == 1) {
            sum << "v0_" << var;
        } else {
            sum << "(+";
            for (int i = 0; i < effective_num_gevs; ++i) {
                sum << " v" << i << "_" << var;
            }
            sum << ")";
        }
        solver->addAssertion("(= " + sum.str() + " " + zero + ")");
        constraint_count++;

        if (verbose)
            std::cout << "      First iteration: sum(v_i_" << var << ") = 0" << std::endl;
    }

    // 2. Ray identity: v_i_x = lambda_i * v_i_x [+ nu_i * v_{i+1}_x]
    for (int gev_idx = 0; gev_idx < effective_num_gevs; ++gev_idx) {
        bool has_next = (gev_idx < effective_num_gevs - 1);
        bool use_nilpotent = settings_.nilpotent_components && has_next;

        for (const auto& var : identity_vars) {
            std::string gev_var = "v" + std::to_string(gev_idx) + "_" + var;
            std::string lambda_var = "lambda_" + std::to_string(gev_idx);

            std::string output_expr;
            if (use_nilpotent) {
                std::string nu_var = "nu_" + std::to_string(gev_idx);
                std::string next_gev_var = "v" + std::to_string(gev_idx + 1) + "_" + var;
                output_expr = "(+ (* " + lambda_var + " " + gev_var + ") (* " + nu_var + " " + next_gev_var + "))";
            } else {
                output_expr = "(* " + lambda_var + " " + gev_var + ")";
            }

            solver->addAssertion("(= " + gev_var + " " + output_expr + ")");
            constraint_count++;

            if (verbose)
                std::cout << "      GEV " << gev_idx << ": " << gev_var
                          << " = " << output_expr << std::endl;
        }
    }

    if (verbose)
        std::cout << "      Added " << constraint_count << " identity variable constraints" << std::endl;
}

// ============================================================================
// CONTRAINTES EIGENVALUE ET NILPOTENT
//
// Si allow_bounded:
//   λᵢ ≥ 0 pour tout i
// Sinon:
//   λᵢ ≥ 1 pour tout i
//   ET (y₁ ≠ 0 ∨ ... ∨ yₙ ≠ 0)
//
// Si nilpotent_components:
//   νᵢ ∈ {0, 1}  pour tout i ∈ [0, n-2]
// Sinon:
//   νᵢ = 0
// ============================================================================

void GeometricTechnique::addEigenvalueAndNilpotentConstraints(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    int constraint_count = 0;

    // Literals adaptés au sort (Int vs Real)
    const std::string zero = lasso_->integer_mode ? "0" : "0.0";
    const std::string one  = lasso_->integer_mode ? "1" : "1.0";

    // Contraintes sur les eigenvalues
    for (int i = 0; i < effective_num_gevs; ++i) {
        std::string lambda_var = "lambda_" + std::to_string(i);

        if (settings_.allow_bounded) {
            // λᵢ ≥ 0
            solver->addAssertion("(>= " + lambda_var + " " + zero + ")");
            if (verbose)
                std::cout << "      lambda_" << i << " >= " << zero << std::endl;
        } else {
            // λᵢ ≥ 1
            solver->addAssertion("(>= " + lambda_var + " " + one + ")");
            if (verbose)
                std::cout << "      lambda_" << i << " >= " << one << std::endl;
        }
        constraint_count++;
    }

    // Si pas allow_bounded: forcer au moins un GEV non-nul
    if (!settings_.allow_bounded && effective_num_gevs > 0) {
        std::ostringstream v_nonzero;
        v_nonzero << "(or";
        for (int i = 0; i < effective_num_gevs; ++i) {
            for (const auto& var : lasso_->program_vars) {
                v_nonzero << " (not (= v" << i << "_" << var << " " << zero << "))";
            }
        }
        v_nonzero << ")";
        solver->addAssertion(v_nonzero.str());
        constraint_count++;

        if (verbose)
            std::cout << "      Forced at least one GEV != 0" << std::endl;
    }

    // Contraintes sur les composantes nilpotentes
    if (effective_num_gevs >= 2) {
        for (int i = 0; i < effective_num_gevs - 1; ++i) {
            std::string nu_var = "nu_" + std::to_string(i);

            if (settings_.nilpotent_components) {
                // νᵢ ∈ {0, 1}
                solver->addAssertion("(or (= " + nu_var + " " + zero + ") (= " + nu_var + " " + one + "))");
                if (verbose)
                    std::cout << "      nu_" << i << " in {0, 1}" << std::endl;
            } else {
                // νᵢ = 0
                solver->addAssertion("(= " + nu_var + " " + zero + ")");
                if (verbose)
                    std::cout << "      nu_" << i << " = " << zero << std::endl;
            }
            constraint_count++;
        }
    }

    if (verbose)
        std::cout << "      Added " << constraint_count << " eigenvalue/nilpotent constraints" << std::endl;
}

// ============================================================================
// EXTRACTION DU GNTA
// ============================================================================

NonTerminationResult GeometricTechnique::extractGNTA(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    NonTerminationResult result;
    result.is_nonterminating = true;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Nettoyer les résultats précédents
    state_init.clear();
    state_honda.clear();
    eigenvectors.clear();
    lambdas.clear();
    nus.clear();

    // Extraire x₀
    for (const auto& var : lasso_->program_vars) {
        std::string init_var = "x0_" + var;
        state_init[var] = solver->getValue(init_var);
    }

    // Extraire x₁
    for (const auto& var : lasso_->program_vars) {
        std::string honda_var = "x1_" + var;
        state_honda[var] = solver->getValue(honda_var);
    }

    // Extraire eigenvectors
    for (int i = 0; i < effective_num_gevs; ++i) {
        std::map<std::string, double> gev;
        for (const auto& var : lasso_->program_vars) {
            std::string gev_var = "v" + std::to_string(i) + "_" + var;
            gev[var] = solver->getValue(gev_var);
        }
        eigenvectors.push_back(gev);
    }

    // Extraire eigenvalues
    for (int i = 0; i < effective_num_gevs; ++i) {
        std::string lambda_var = "lambda_" + std::to_string(i);
        lambdas.push_back(solver->getValue(lambda_var));
    }

    // Extraire composantes nilpotentes
    if (settings_.nilpotent_components && effective_num_gevs >= 2) {
        for (int i = 0; i < effective_num_gevs - 1; ++i) {
            std::string nu_var = "nu_" + std::to_string(i);
            nus.push_back(solver->getValue(nu_var));
        }
    }

    // Déterminer le type de résultat
    if (isFixpoint()) {
        result.type = NonTerminationResult::Type::GEOMETRIC_FIXPOINT;
        result.description = "Fixpoint with infinite repetition";
        result.witness_state = state_honda;
    } else {
        result.type = NonTerminationResult::Type::GEOMETRIC_UNBOUNDED;
        result.description = "Geometric unbounded execution found";
        result.witness_state = state_honda;
    }

    // Construire la preuve détaillée
    std::ostringstream proof;
    proof << "x0={";
    bool first = true;
    for (const auto& [var, val] : state_init) {
        if (!first) proof << ", ";
        proof << var << "=" << val;
        first = false;
    }
    proof << "},\nx1={";
    first = true;
    for (const auto& [var, val] : state_honda) {
        if (!first) proof << ", ";
        proof << var << "=" << val;
        first = false;
    }
    proof << "},\n";
    for (int i = 0; i < effective_num_gevs; ++i) {
        proof << "y" << i << "={";
        first = true;
        for (const auto& [var, val] : eigenvectors[i]) {
            if (!first) proof << ", ";
            proof << var << "=" << val;
            first = false;
        }
        proof << "}, L" << i << "=" << lambdas[i];
        proof << "\n";
    }
    if (nus.size() > 0) {
        proof << "nu" << 0 << "=" << nus[0];
        for (size_t i = 1; i < nus.size(); ++i) {
            proof << ", nu" << i << "=" << nus[i];
        }
    }
    result.proof_details = proof.str();

    if (verbose) {
        std::cout << "\n  Initial state (x0):" << std::endl;
        for (const auto& [var, val] : state_init) {
            std::cout << "    " << var << " = " << val << std::endl;
        }

        std::cout << "\n  Honda state (x1):" << std::endl;
        for (const auto& [var, val] : state_honda) {
            std::cout << "    " << var << " = " << val << std::endl;
        }

        for (int i = 0; i < effective_num_gevs; ++i) {
            std::cout << "\n  Eigenvector y" << i << ":" << std::endl;
            for (const auto& [var, val] : eigenvectors[i]) {
                std::cout << "    v" << i << "_" << var << " = " << val << std::endl;
            }
            std::cout << "  Eigenvalue lambda_" << i << ": " << lambdas[i] << std::endl;
        }

        for (size_t i = 0; i < nus.size(); ++i) {
            std::cout << "  Nilpotent nu_" << i << ": " << nus[i] << std::endl;
        }
    }

    return result;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void GeometricTechnique::printResult(const NonTerminationResult& result) const
{
    std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║       GEOMETRIC NONTERMINATION ARGUMENT               ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;

    if (!result.is_nonterminating) {
        std::cout << "\n  No GNTA found" << std::endl;
        std::cout << "  " << result.description << std::endl;
        return;
    }

    std::cout << "\n  NON-TERMINATING: " << result.description << std::endl;
    std::cout << "\n  Type: " << (isFixpoint() ? "Fixpoint" : "Unbounded Execution") << std::endl;
    std::cout << "  Number of GEVs: " << getNumGEVs() << std::endl;
    std::cout << "  Nilpotent components: " << (settings_.nilpotent_components ? "enabled" : "disabled") << std::endl;
    std::cout << "  Allow bounded: " << (settings_.allow_bounded ? "yes" : "no") << std::endl;

    std::cout << "\n  Infinite execution trace:" << std::endl;
    std::cout << "    x0 -> x1 -> x1 + Y*(sum J^i)*1 -> ..." << std::endl;

    if (!result.proof_details.empty()) {
        std::cout << "\n  Proof: " << result.proof_details << std::endl;
    }
}

// ============================================================================
// VALIDATION
// ============================================================================

bool GeometricTechnique::validateConfiguration() const {
    return initialized_ && lasso_ != nullptr && settings_.num_gevs > 0;
}

// ============================================================================
// ACCESSEURS
// ============================================================================

void GeometricTechnique::setSettings(const GeometricNonTerminationSettings& settings) {
    settings_ = settings;
}

const GeometricNonTerminationSettings& GeometricTechnique::getSettings() const {
    return settings_;
}

int GeometricTechnique::getNumGEVs() const {
    return settings_.num_gevs;
}
