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

AnalysisResult GeometricTechnique::analyze(
    std::shared_ptr<SMTSolver> solver) {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    ProofCertificate proof;
    proof.technique_name = getName();

    if (!initialized_ || !lasso_) {
        proof.description = "Technique not initialized";
        proof_ = proof;
        return proof_.status;
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

    if (sat) {
        if (verbose)
            std::cout << "    SAT - Geometric nontermination argument found!" << std::endl;

        if (verbose)
            std::cout << "\n[4/4] Extracting GNTA..." << std::endl;
        proof = extractGNTA(solver, settings_.num_gevs);

        if (verbose) {
            std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║  NON-TERMINATION PROVED (Geometric)                  ║" << std::endl;
            std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
        }
    } else {
        if (verbose)
            std::cout << "    UNSAT - No geometric nontermination argument found" << std::endl;
        proof.description = "No geometric nontermination argument found";
    }

    solver->pop();

    proof_ = proof;
    return proof_.status;
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
    // En mode LINEAR, lambda est fixé à 1 (pas de variable SMT)
    if (settings_.analysis_type != GeometricNonTerminationSettings::AnalysisType::LINEAR) {
        for (int i = 0; i < effective_num_gevs; ++i) {
            std::string lambda_var = "lambda_" + std::to_string(i);
            if (!solver->variableExists(lambda_var)) {
                solver->declareVariable(lambda_var, sort);
            }
        }
    }

    // Composantes nilpotentes (νᵢ) pour i = 0..n-2
    // mGEVs.size() == nus.size() + 1
    // if (settings_.nilpotent_components && effective_num_gevs >= 2) {
    //     for (int i = 0; i < effective_num_gevs - 1; ++i) {
    //         std::string nu_var = "nu_" + std::to_string(i);
    //         if (!solver->variableExists(nu_var)) {
    //             solver->declareVariable(nu_var, sort);
    //         }
    //     }
    // }

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

    // 2. Première itération + rays avec cohérence de branche DNF
    if (verbose)
        std::cout << "    • Adding combined loop constraints (branch-consistent, "
                  << lasso_->loop.polyhedra.size() << " polyhedron/polyhedra)" << std::endl;
    addCombinedLoopConstraints(solver, effective_num_gevs);

    // 3. Contraintes d'identité pour variables inchangées (in_ssa == out_ssa)
    if (verbose)
        std::cout << "    • Adding identity variable constraints" << std::endl;
    addIdentityVariableConstraints(solver, effective_num_gevs);

    // 4. Contraintes sur eigenvalues et nilpotent
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
    // En mode LINEAR : lambda_i = 1 → v_i_x = v_i_x [+ nu_i * v_{i+1}_x]
    const bool linear_mode = (settings_.analysis_type ==
        GeometricNonTerminationSettings::AnalysisType::LINEAR);

    for (int gev_idx = 0; gev_idx < effective_num_gevs; ++gev_idx) {
        bool has_next = (gev_idx < effective_num_gevs - 1);
        bool use_nilpotent = settings_.nilpotent_components && has_next;

        for (const auto& var : identity_vars) {
            std::string gev_var = "v" + std::to_string(gev_idx) + "_" + var;

            std::string output_expr;
            if (use_nilpotent) {
                std::string next_gev_var = "v" + std::to_string(gev_idx + 1) + "_" + var;
                if (linear_mode) {
                    // λ=1, nu ∈ {0,1} : v_i_x = v_i_x + nu * v_{i+1}_x
                    // Pour une identity var, les ray constraints se neutralisent
                    // (coef_out - coef_in = 0), donc on doit imposer v_i_x = 0
                    // explicitement. Nu=1 forcerait aussi v_{i+1}_x = 0, mais
                    // imposer v_i_x = 0 est suffisant et plus simple.
                    solver->addAssertion("(= " + gev_var + " " + zero + ")");
                    constraint_count++;
                    if (verbose)
                        std::cout << "      GEV " << gev_idx << ": " << gev_var
                                  << " = 0 (identity var, linear+nilpotent mode)" << std::endl;
                    continue;
                } else {
                    std::string nu_var = "nu_" + std::to_string(gev_idx);
                    std::string lambda_var = "lambda_" + std::to_string(gev_idx);
                    output_expr = "(+ (* " + lambda_var + " " + gev_var + ") (* " + nu_var + " " + next_gev_var + "))";
                }
            } else {
                if (linear_mode) {
                    // λ=1, pas de nilpotent : v_i_x = 1 * v_i_x → v_i_x = v_i_x
                    // La contrainte ray homogène pour une identity var est :
                    //   coef_out * v_i_x + coef_in * v_i_x >= 0  (les deux coefficients s'annulent)
                    // ce qui est trivial. Il faut donc imposer explicitement v_i_x = 0.
                    solver->addAssertion("(= " + gev_var + " " + zero + ")");
                    constraint_count++;
                    if (verbose)
                        std::cout << "      GEV " << gev_idx << ": " << gev_var
                                  << " = 0 (identity var, linear mode)" << std::endl;
                    continue;
                } else {
                    std::string lambda_var = "lambda_" + std::to_string(gev_idx);
                    output_expr = "(* " + lambda_var + " " + gev_var + ")";
                }
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
// HELPERS PAR POLYÈDRE (utilisés par addCombinedLoopConstraints)
// ============================================================================

std::vector<std::string> GeometricTechnique::buildFirstIterConstraintsForPoly(
    const std::vector<LinearInequality>& poly,
    int effective_num_gevs,
    std::shared_ptr<SMTSolver> solver)
{
    std::vector<std::string> result;

    for (const auto& ineq : poly) {
        std::ostringstream lhs;
        lhs << "(+";
        bool has_terms = false;

        for (const auto& [var, coef] : ineq.coefficients) {
            if (!coef.isConstant() || std::abs(coef.constant) <= 1e-9) continue;

            bool is_input = false, is_output = false;
            std::string prog_var;
            for (const auto& [vp, ssa_in] : lasso_->loop.var_to_ssa_in) {
                if (ssa_in == var) { is_input = true; prog_var = vp; break; }
            }
            for (const auto& [vp, ssa_out] : lasso_->loop.var_to_ssa_out) {
                if (ssa_out == var) { is_output = true; prog_var = vp; break; }
            }

            if (is_input && is_output) {
                lhs << " (* " << formatNumber(coef.constant) << " x1_" << prog_var << ")";
                has_terms = true;
            } else if (is_output) {
                std::ostringstream sum;
                sum << "(+ x1_" << prog_var;
                for (int i = 0; i < effective_num_gevs; ++i)
                    sum << " v" << i << "_" << prog_var;
                sum << ")";
                lhs << " (* " << formatNumber(coef.constant) << " " << sum.str() << ")";
                has_terms = true;
            } else if (is_input) {
                lhs << " (* " << coef.constant << " x1_" << prog_var << ")";
                has_terms = true;
            } else {
                std::string iter_var = var + "__gnta_iter";
                if (!solver->variableExists(iter_var))
                    solver->declareVariable(iter_var, "Int");
                lhs << " (* " << coef.constant << " " << iter_var << ")";
                has_terms = true;
            }
        }

        if (ineq.constant.isConstant()) {
            lhs << " " << formatNumber(ineq.constant.constant);
            has_terms = true;
        }
        lhs << ")";

        if (has_terms) {
            std::ostringstream c;
            c << "(" << (ineq.strict ? ">" : ">=") << " " << lhs.str() << " 0)";
            result.push_back(c.str());
        }
    }
    return result;
}

std::vector<std::vector<std::string>> GeometricTechnique::buildRayConstraintsForPoly(
    const std::vector<LinearInequality>& poly,
    int gev_idx,
    int effective_num_gevs,
    std::shared_ptr<SMTSolver> solver)
{
    const bool linear_mode = (settings_.analysis_type ==
        GeometricNonTerminationSettings::AnalysisType::LINEAR);
    const bool has_next_gev = (gev_idx < effective_num_gevs - 1);
    const bool use_nilpotent = settings_.nilpotent_components && has_next_gev;

    // Enumerate nu values: {0,1} in linear-nilpotent mode, {-1} (sentinel) otherwise
    std::vector<int> nu_vals = (linear_mode && use_nilpotent) ? std::vector<int>{0, 1}
                                                               : std::vector<int>{-1};

    std::vector<std::vector<std::string>> all_branches;

    for (int nu_enum : nu_vals) {
        std::vector<std::string> branch;

        for (const auto& ineq : poly) {
            std::ostringstream lhs;
            lhs << "(+";
            bool has_terms = false;

            for (const auto& [var, coef] : ineq.coefficients) {
                if (!coef.isConstant() || std::abs(coef.constant) <= 1e-9) continue;

                bool is_input = false, is_output = false;
                std::string prog_var;
                for (const auto& [vp, ssa_in] : lasso_->loop.var_to_ssa_in) {
                    if (ssa_in == var) { is_input = true; prog_var = vp; break; }
                }
                for (const auto& [vp, ssa_out] : lasso_->loop.var_to_ssa_out) {
                    if (ssa_out == var) { is_output = true; prog_var = vp; break; }
                }

                if (is_input && is_output) {
                    std::string gev_var = "v" + std::to_string(gev_idx) + "_" + prog_var;
                    lhs << " (* " << formatNumber(coef.constant) << " " << gev_var << ")";
                    has_terms = true;
                } else if (is_output) {
                    std::string gev_var = "v" + std::to_string(gev_idx) + "_" + prog_var;
                    if (use_nilpotent) {
                        std::string next_gev = "v" + std::to_string(gev_idx + 1) + "_" + prog_var;
                        if (linear_mode) {
                            // nu ∈ {0,1} enumerated as concrete constants
                            if (nu_enum == 0)
                                lhs << " (* " << formatNumber(coef.constant) << " " << gev_var << ")";
                            else
                                lhs << " (* " << formatNumber(coef.constant)
                                    << " (+ " << gev_var << " " << next_gev << "))";
                        } else {
                            std::string nu_var    = "nu_"     + std::to_string(gev_idx);
                            std::string lam_var   = "lambda_" + std::to_string(gev_idx);
                            lhs << " (* " << coef.constant
                                << " (+ (* " << lam_var << " " << gev_var << ")"
                                << " (* " << nu_var << " " << next_gev << ")))";
                        }
                    } else {
                        if (linear_mode)
                            lhs << " (* " << formatNumber(coef.constant) << " " << gev_var << ")";
                        else {
                            std::string lam_var = "lambda_" + std::to_string(gev_idx);
                            lhs << " (* " << coef.constant
                                << " (* " << lam_var << " " << gev_var << "))";
                        }
                    }
                    has_terms = true;
                } else if (is_input) {
                    std::string gev_var = "v" + std::to_string(gev_idx) + "_" + prog_var;
                    lhs << " (* " << formatNumber(coef.constant) << " " << gev_var << ")";
                    has_terms = true;
                } else {
                    std::string ray_var = var + "__gnta_ray_" + std::to_string(gev_idx);
                    if (!solver->variableExists(ray_var))
                        solver->declareVariable(ray_var, "Int");
                    lhs << " (* " << coef.constant << " " << ray_var << ")";
                    has_terms = true;
                }
            }

            // RAYS=TRUE: no constant (homogeneous), always non-strict
            // For a ray direction y: if the original loop has a*x > c (strict),
            // the condition for y to stay in the half-space is a*y >= 0 (non-strict),
            // because a*(x + t*y) > c holds for all t >= 0 iff a*y >= 0.
            lhs << ")";

            if (has_terms) {
                std::ostringstream c;
                c << "(>= " << lhs.str() << " 0)";
                branch.push_back(c.str());
            }
        }
        all_branches.push_back(branch);
    }
    return all_branches;
}

// ============================================================================
// COMBINED LOOP CONSTRAINTS (branch-consistent)
//
//   for polyhedron in loop.polyhedra:
//       t_honda = generateConstraint(loop, polyhedron, x1, x1+y, false)
//       t_gev_i = generateConstraint(loop, polyhedron, y_i, lambda*y_i, true)
//       disjunction.add(AND(t_honda, t_gev_0, ..., t_gev_n))
//   assert OR(disjunction)
//
// Garantit que la première itération et TOUS les rays utilisent le MÊME polyèdre.
// ============================================================================

void GeometricTechnique::addCombinedLoopConstraints(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    std::vector<std::vector<std::string>> per_poly_clauses;

    for (const auto& poly : lasso_->loop.polyhedra) {
        std::vector<std::string> clause;

        // Première itération : Loop(x1, x1 + y0 + ... + yn) avec ce polyèdre
        auto fi = buildFirstIterConstraintsForPoly(poly, effective_num_gevs, solver);
        clause.insert(clause.end(), fi.begin(), fi.end());

        // Rays pour chaque GEV avec LE MÊME polyèdre
        for (int gev_idx = 0; gev_idx < effective_num_gevs; ++gev_idx) {
            auto ray_branches = buildRayConstraintsForPoly(poly, gev_idx, effective_num_gevs, solver);

            if (ray_branches.size() == 1) {
                // Cas standard (pas d'énumération nu) : ajouter directement à la clause
                clause.insert(clause.end(), ray_branches[0].begin(), ray_branches[0].end());
            } else if (ray_branches.size() > 1) {
                // Mode linear-nilpotent : sous-disjonction sur nu ∈ {0,1}
                std::ostringstream sub_dnf;
                sub_dnf << "(or";
                for (const auto& branch : ray_branches) {
                    sub_dnf << " (and";
                    for (const auto& c : branch) sub_dnf << " " << c;
                    sub_dnf << ")";
                }
                sub_dnf << ")";
                clause.push_back(sub_dnf.str());
            }
        }

        if (!clause.empty())
            per_poly_clauses.push_back(clause);
    }

    if (verbose)
        std::cout << "      Combined loop constraints: " << per_poly_clauses.size()
                  << " polyhedron/polyhedra" << std::endl;

    // Émettre en DNF externe
    if (per_poly_clauses.size() == 1) {
        for (const auto& c : per_poly_clauses[0])
            solver->addAssertion(c);
    } else if (per_poly_clauses.size() > 1) {
        std::ostringstream dnf;
        dnf << "(or";
        for (const auto& clause : per_poly_clauses) {
            dnf << " (and";
            for (const auto& c : clause) dnf << " " << c;
            dnf << ")";
        }
        dnf << ")";
        solver->addAssertion(dnf.str());
    }
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
    // En mode LINEAR, lambda est fixé à 1 : pas de variable, pas de contrainte
    if (settings_.analysis_type != GeometricNonTerminationSettings::AnalysisType::LINEAR) {
        for (int i = 0; i < effective_num_gevs; ++i) {
            std::string lambda_var = "lambda_" + std::to_string(i);

            if (settings_.allow_bounded) {
                solver->addAssertion("(>= " + lambda_var + " " + zero + ")");
                if (verbose)
                    std::cout << "      lambda_" << i << " >= " << zero << std::endl;
            } else {
                solver->addAssertion("(>= " + lambda_var + " " + one + ")");
                if (verbose)
                    std::cout << "      lambda_" << i << " >= " << one << std::endl;
            }
            constraint_count++;
        }
    } else {
        if (verbose)
            std::cout << "      lambda fixed = 1 (LINEAR mode, no SMT variable)" << std::endl;
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
    // if (effective_num_gevs >= 2) {
    //     for (int i = 0; i < effective_num_gevs - 1; ++i) {
    //         std::string nu_var = "nu_" + std::to_string(i);

    //         if (settings_.nilpotent_components) {
    //             // νᵢ ∈ {0, 1}
    //             solver->addAssertion("(or (= " + nu_var + " " + zero + ") (= " + nu_var + " " + one + "))");
    //             if (verbose)
    //                 std::cout << "      nu_" << i << " in {0, 1}" << std::endl;
    //         } else {
    //             // νᵢ = 0
    //             solver->addAssertion("(= " + nu_var + " " + zero + ")");
    //             if (verbose)
    //                 std::cout << "      nu_" << i << " = " << zero << std::endl;
    //         }
    //         constraint_count++;
    //     }
    // }

    if (verbose)
        std::cout << "      Added " << constraint_count << " eigenvalue/nilpotent constraints" << std::endl;
}

// ============================================================================
// EXTRACTION DU GNTA
// ============================================================================

ProofCertificate GeometricTechnique::extractGNTA(
    std::shared_ptr<SMTSolver> solver, int effective_num_gevs)
{
    ProofCertificate result;
    result.status = AnalysisResult::NON_TERMINATING;
    result.technique_name = getName();
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
    // En mode LINEAR : lambda est fixé à 1, pas de variable SMT → pousser 1.0 directement
    const bool linear_mode_extract = (settings_.analysis_type ==
        GeometricNonTerminationSettings::AnalysisType::LINEAR);
    for (int i = 0; i < effective_num_gevs; ++i) {
        if (linear_mode_extract) {
            lambdas.push_back(1.0);
        } else {
            std::string lambda_var = "lambda_" + std::to_string(i);
            lambdas.push_back(solver->getValue(lambda_var));
        }
    }

    // Extraire composantes nilpotentes
    // if (settings_.nilpotent_components && effective_num_gevs >= 2) {
    //     for (int i = 0; i < effective_num_gevs - 1; ++i) {
    //         std::string nu_var = "nu_" + std::to_string(i);
    //         nus.push_back(solver->getValue(nu_var));
    //     }
    // }

    // Déterminer le type de résultat
    if (isFixpoint()) {
        result.description = "Fixpoint with infinite repetition";
        result.nt_witness_state = state_honda;
    } else {
        result.description = "Geometric unbounded execution found";
        result.nt_witness_state = state_honda;
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
