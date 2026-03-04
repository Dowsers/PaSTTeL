#include <iostream>
#include <iomanip>
#include <sstream>

#include "termination/ranking_and_invariant_validator.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// Helper : extrait la valeur scalaire d'une variable SSA, en ignorant les Array.
// var_prog : nom de programme (ex: "balance"), ssa : nom SSA (ex: "v_balance_1")
static void extractScalarValue(
    const std::string& var_prog,
    const std::string& ssa,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    std::map<std::string, double>& counterexample)
{
    auto it = lasso.var_sorts.find(var_prog);
    if (it != lasso.var_sorts.end()) {
        const std::string& sort = it->second;
        if (sort.find("Array") != std::string::npos) return;
    }
    counterexample[var_prog] = solver->getValue(ssa);
}


// ============================================================================
// CONSTRUCTEUR
// ============================================================================

RankingAndInvariantValidator::RankingAndInvariantValidator() {
}

// ============================================================================
// VALIDATION PRINCIPALE
// ============================================================================

RankingAndInvariantValidator::ValidationResult RankingAndInvariantValidator::validate(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver)
{
    const RankingFunction& ranking_function = argument.ranking_function;
    const std::vector<SupportingInvariant>& supporting_invariants = argument.supporting_invariants;

    ValidationResult result;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    result.is_valid = false;
    result.all_si_valid = false;
    
    if(verbose) {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║    VALIDATION POST-SYNTHÈSE                          ║" << std::endl;
        std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    }
    // ========================================================================
    // PARTIE 1 : VALIDATION DE LA FONCTION DE RANKING
    // ========================================================================

    registerProgramVariablesToSolver(solver, lasso);

    if(verbose) {
        std::cout << "\n╭─ Fonction de Ranking ─────────────────────────────────╮" << std::endl;
        // Étape 1.1 : Non-trivialité
        std::cout << "[1/3] Vérification de la non-trivialité..." << std::endl;
    }
    result.rf_non_trivial_check = checkRFNonTriviality(ranking_function);
    
    if (!result.rf_non_trivial_check) {
        result.error_message = "Ranking function est triviale (tous les coefficients nuls)";
        if(verbose) {
            std::cout << "  ❌ ÉCHEC : " << result.error_message << std::endl;
            std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;
        }
        return result;
    }
    if(verbose) {
        std::cout << "  ✅ OK : Au moins un coefficient non-nul" << std::endl;

        // Étape 1.2 : Bounded
        std::cout << "[2/3] Vérification bounded (f(x) ≥ 0 dans le loop)..." << std::endl;
    }

    if(verbose) {
        std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;

    // ========================================================================
    // PARTIE 2 : VALIDATION DES SUPPORTING INVARIANTS
    // ========================================================================

        std::cout << "╭─ Supporting Invariants ───────────────────────────────╮" << std::endl;
        std::cout << "  Nombre de SI synthétisés : " << supporting_invariants.size() << std::endl;
    }

    if (supporting_invariants.empty()) {
        if(verbose)
            std::cout << "  ℹ️  Aucun SI synthétisé" << std::endl;
        result.all_si_valid = true;  // Vacuously true
    } else {
        // Compteurs pour statistiques
        int num_trivial_true = 0;
        int num_trivial_false = 0;
        int num_non_trivial = 0;
        int num_valid_non_trivial = 0;

        for (size_t i = 0; i < supporting_invariants.size(); ++i) {
            if(verbose)
                std::cout << "\n  ┌─ SI #" << i << " ────────────────────────────────────┐" << std::endl;
            SIValidationResult si_result = validateSingleSI(i, supporting_invariants[i], lasso, solver);
            result.si_results.push_back(si_result);

            // Comptabiliser les résultats
            if (si_result.is_false_check) {
                num_trivial_false++;
                if(verbose)
                    std::cout << "  │ !  TRIVIAL FALSE" << std::endl;
            } else if (si_result.is_true_check) {
                num_trivial_true++;
                if(verbose)
                    std::cout << "  │ ✅ TRIVIAL TRUE" << std::endl;
            } else {
                num_non_trivial++;
                if (si_result.is_valid) {
                    num_valid_non_trivial++;
                    valid_sis.push_back(supporting_invariants[i]);
                    if(verbose)
                        std::cout << "  │ ✅ VALIDE (non-trivial)" << std::endl;
                } else {
                    if(verbose)
                        std::cout << "  │ ❌ INVALIDE : " << si_result.error_message << std::endl;
                }
            }

            if(verbose) {
                std::cout << "  │   • isFalse()      : " << (si_result.is_false_check ? "❌ FAUX" : "✅") << std::endl;
                std::cout << "  │   • isTrue()       : " << (si_result.is_true_check ? "✅ VRAI" : "➖") << std::endl;

                if (!si_result.is_false_check && !si_result.is_true_check) {
                    std::cout << "  │   • Initiation     : " << (si_result.initiation_check ? "✅" : "❌") << std::endl;
                    std::cout << "  │   • Compatible     : " << (si_result.compatible_check ? "✅" : "❌") << std::endl;
                    std::cout << "  │   • Consécution    : " << (si_result.consecution_check ? "✅" : "❌") << std::endl;
                }

                std::cout << "  └─────────────────────────────────────────────────┘" << std::endl;
            }
        }

        if(verbose) {
            std::cout << "\n    Statistiques SI :" << std::endl;
            std::cout << "     • Triviaux TRUE  : " << num_trivial_true << " (filtrés)" << std::endl;
            std::cout << "     • Triviaux FALSE : " << num_trivial_false << " (rejetés)" << std::endl;
            std::cout << "     • Non-triviaux   : " << num_non_trivial << std::endl;
            std::cout << "     • Valides (non-t): " << num_valid_non_trivial << std::endl;
        }

        if (num_trivial_false > 0) {
            result.all_si_valid = false;
            if(verbose)
                std::cout << "\n  ❌ Échec : " << num_trivial_false << " SI trivialement FAUX" << std::endl;
        } else {
            // Tous les SI non-triviaux invalides sont ignorés :
            // on retente la décroissance RF avec seulement les valid_sis
            result.all_si_valid = true;
            if(verbose) {
                if (num_trivial_true > 0)
                    std::cout << "\n  ✅ SI triviale(s) TRUE acceptée(s)" << std::endl;
                if (num_valid_non_trivial > 0)
                    std::cout << "\n  ✅ " << num_valid_non_trivial << "/" << num_non_trivial
                            << " SI non-triviaux valides" << std::endl;
                if (num_valid_non_trivial == 0 && num_trivial_true == 0)
                    std::cout << "\n  ℹ️  Aucun SI valide — vérification RF sans SI" << std::endl;
            }
        }
    }
    if(verbose)
        std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;

    // ========================================================================
    // PARTIE 3 : VÉRIFICATION RF BOUNDED + DECREASING avec les SI valides
    // ========================================================================

    if(verbose) {
        std::cout << "╭─ RF bounded + décroissante (avec " << valid_sis.size() << " SI valide(s)) ─╮" << std::endl;
        std::cout << "[2/3] Vérification bounded (f(x) ≥ 0 sous les SI)..." << std::endl;
    }

    result.rf_bounded_check = checkRFBounded(
        ranking_function, valid_sis, lasso, solver, result.rf_counterexample);

    if (!result.rf_bounded_check) {
        result.error_message = "Ranking function n'est pas bornée : f(x) < 0 possible";
        if(verbose) {
            std::cout << "  ❌ ÉCHEC : " << result.error_message << std::endl;
            std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;
        }
    } else {
        if(verbose) {
            std::cout << "  ✅ OK : f(x) ≥ 0 sous les SI" << std::endl;
            std::cout << "[3/3] Vérification decreasing (f(x) - f(x') ≥ δ)..." << std::endl;
        }

        result.rf_decreasing_check = checkRFDecreasing(
            ranking_function, valid_sis, lasso, solver, ranking_function.delta, result.rf_counterexample);

        if (!result.rf_decreasing_check) {
            result.error_message = "Ranking function ne décroît pas strictement (même sans SI invalides)";
            if(verbose) {
                std::cout << "  ❌ ÉCHEC : " << result.error_message << std::endl;
                std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;
            }
        } else if(verbose) {
            std::cout << "  ✅ OK : f(x) - f(x') ≥ " << ranking_function.delta << std::endl;
            std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;
        }
    }

    // ========================================================================
    // RÉSULTAT FINAL
    // ========================================================================

    result.is_valid = result.all_si_valid
                && result.rf_non_trivial_check
                && result.rf_bounded_check
                && result.rf_decreasing_check;
    
    if(verbose) {
        if (result.is_valid) {
            std::cout << "╔═══════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║  ✅ VALIDATION RÉUSSIE !                             ║" << std::endl;
            std::cout << "║     Argument de termination VALIDE                   ║" << std::endl;
            std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
        } else if (!result.all_si_valid) {
            result.error_message = "Un ou plusieurs Supporting Invariants sont invalides";
            std::cout << "╔═══════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║  ❌ VALIDATION ÉCHOUÉE                               ║" << std::endl;
            if(!result.error_message.empty())
            std::cout << "║     " << result.error_message << std::string(10, ' ') << "║" << std::endl;
            std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
        }
    }

    return result;
}

// ============================================================================
// ENREGISTREMENT DES VARIABLES INPUT ET OUTPUT DU PROGRAMME
// ============================================================================

void RankingAndInvariantValidator::registerProgramVariablesToSolver(
    std::shared_ptr<SMTSolver> solver,
    const LassoProgram& lasso) {

    lasso.declareSolverContext(solver);
}


// ============================================================================
// VALIDATION D'UN SEUL SI
// ============================================================================

RankingAndInvariantValidator::SIValidationResult RankingAndInvariantValidator::validateSingleSI(
    int si_index,
    const SupportingInvariant& si,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver)
{
    SIValidationResult result;
    result.si_index = si_index;
    result.is_valid = false;
    result.is_false_check = false;
    result.is_true_check = false;
    result.initiation_check = false;
    result.consecution_check = false;
    
    // ========================================================================
    // ÉTAPE 1 : VÉRIFICATIONS TRIVIALES
    // ========================================================================
    
    // 1.1 : Vérifier si le SI est trivialement FAUX
    result.is_false_check = checkSIIsFalse(si);
    if (result.is_false_check) {
        result.error_message = "SI est trivialement FAUX (isFalse() == true)";
        return result;
    }
    
    // 1.2 : Vérifier si le SI est trivialement VRAI (on l'ignore alors)
    result.is_true_check = checkSIIsTrue(si);
    if (result.is_true_check) {
        // SI trivial "true" → on l'accepte mais on ne fait pas de checks SMT
        result.is_valid = true;
        result.initiation_check = true;
        result.consecution_check = true;
        return result;
    }
    
    // 1.3 : Vérifier la non-trivialité (au moins un coefficient de variable non-nul)
    bool non_trivial = checkSINonTriviality(si);
    if (!non_trivial) {
        result.error_message = "SI a seulement une constante (pas de variables)";
        // Note: ce cas devrait être couvert par isFalse() ou isTrue()
        return result;
    }
    
    // ========================================================================
    // ÉTAPE 2 : VÉRIFICATIONS SMT - Seulement si pas trivial
    // ========================================================================
    
    // 2.1 : Initiation (si pas de stem, on skip)
    if (!lasso.hasNoStem()) {
        result.initiation_check = checkSIInitiation(si, lasso, solver, result.counterexample);
        if (!result.initiation_check) {
            result.error_message = "Échec initiation : stem n'implique pas SI";
            return result;
        }
    } else {
        // Pas de stem → initiation vacuously true
        result.initiation_check = true;
    }
    
    // 2.2 : Compatibilité avec loop guard (vérifier pas vacuously valid)
    result.compatible_check = checkSICompatibleWithLoop(si, lasso, solver, result.counterexample);
    if (!result.compatible_check) {
        result.error_message = "SI incompatible avec loop guard (vacuously valid)";
        return result;
    }
    
    // 2.3 : Consécution
    result.consecution_check = checkSIConsecution(si, lasso, solver, result.counterexample);
    if (!result.consecution_check) {
        result.error_message = "Échec consécution : SI n'est pas inductif";
        return result;
    }
    
    // Tout est OK !
    result.is_valid = true;
    return result;
}

// ============================================================================
// VÉRIFICATIONS TRIVIALES (RAPIDES) - SUPPORTING INVARIANTS
// ============================================================================

bool RankingAndInvariantValidator::checkSIIsFalse(
    const SupportingInvariant& si) const
{
    // SI est trivialement FAUX si :
    // - Pas de variables (seulement une constante c)
    // - Non-strict (>=) : c < 0
    // - Strict (>)      : c <= 0
    
    bool has_variables = false;
    for (const auto& [var, coef] : si.coefficients) {
        if (coef != 0) {
            has_variables = true;
            break;
        }
    }

    if (has_variables) {
        return false;  // Pas trivialement faux si a des variables
    }

    // Seulement une constante
    if (si.is_strict) {
        // SI: c > 0 est FAUX si c <= 0
        return si.constant <= 0;
    } else {
        // SI: c >= 0 est FAUX si c < 0
        return si.constant < 0;
    }
}

bool RankingAndInvariantValidator::checkSIIsTrue(
    const SupportingInvariant& si) const
{
    // SI est trivialement VRAI si :
    // - Pas de variables (seulement une constante c)
    // - Non-strict (>=) : c >= 0
    // - Strict (>)      : c > 0
    
    bool has_variables = false;
    for (const auto& [var, coef] : si.coefficients) {
        if (coef != 0) {
            has_variables = true;
            break;
        }
    }

    if (has_variables) {
        return false;  // Pas trivialement vrai si a des variables
    }

    // Seulement une constante
    if (si.is_strict) {
        // SI: c > 0 est VRAI si c > 0
        return si.constant > 0;
    } else {
        // SI: c >= 0 est VRAI si c >= 0
        return si.constant >= 0;
    }
}

bool RankingAndInvariantValidator::checkSINonTriviality(
    const SupportingInvariant& si) const
{
    // Au moins un coefficient de variable doit être non-nul
    for (const auto& [var, coef] : si.coefficients) {
        if (coef != 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// VÉRIFICATIONS SMT - SUPPORTING INVARIANTS
// ============================================================================

// Cherche un contre-exemple où stem(x, x') ∧ ¬SI(x') est SAT
bool RankingAndInvariantValidator::checkSIInitiation(
    const SupportingInvariant& si,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    std::map<std::string, double>& counterexample)
{
    solver->push();
    
    // Ajouter les contraintes du stem
    for (const auto& poly : lasso.stem.polyhedra) {
        for (const auto& ineq : poly) {
            std::string smt_constraint = ineq.toSMTLib2();
            solver->addAssertion(smt_constraint);
        }
    }
    
    // Construire ¬SI(x') : SI évalué sur les out_vars du stem (= in_vars du loop)
    std::ostringstream neg_si;
    neg_si << "(" << (si.is_strict ? "<=" : "<") << " (+";

    bool has_terms = false;
    for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
        const std::string& prog_var = lasso.program_vars[i];
        auto it = si.coefficients.find(prog_var);
        if (it != si.coefficients.end() && it->second != 0) {
            neg_si << " (* " << it->second << " " << lasso.stem.getSSAVar(prog_var, true) << ")";
            has_terms = true;
        }
    }

    if (si.constant != 0 || !has_terms) {
        neg_si << " " << si.constant;
    }
    
    neg_si << ") 0)";
    solver->addAssertion(neg_si.str());
    bool sat = solver->checkSat();
    
    if (sat) {
        for (const auto& [var_prog, ssa_out] : lasso.stem.var_to_ssa_out) {
            extractScalarValue(var_prog, ssa_out, lasso, solver, counterexample);
        }
    }
    
    solver->pop();
    return !sat;  // Valide si UNSAT
}

// Cherche un contre-exemple où SI(x) ∧ loop(x, x') ∧ ¬SI(x') est SAT
bool RankingAndInvariantValidator::checkSIConsecution(
    const SupportingInvariant& si,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    std::map<std::string, double>& counterexample)
{
    solver->push();
    
    std::ostringstream si_x;
    si_x << "(" << (si.is_strict ? ">" : ">=") << " (+";
    
    for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
        const std::string& prog_var = lasso.program_vars[i];
        auto it = si.coefficients.find(prog_var);
        if (it != si.coefficients.end() && it->second != 0) {
            si_x << " (* " << it->second << " " << lasso.loop.getSSAVar(prog_var, false) << ")";
        }
    }

    si_x << " " << si.constant << ") 0)";
    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << si_x.str() << std::endl;
    solver->addAssertion(si_x.str());

    // Ajouter les contraintes du loop
    for (const auto& poly : lasso.loop.polyhedra) {
        for (const auto& ineq : poly) {
            std::string smt_constraint = ineq.toSMTLib2();
            solver->addAssertion(smt_constraint);
        }
    }

    // Ajouter ¬SI(x') avec out_vars de loop
    std::ostringstream neg_si_xprime;
    neg_si_xprime << "(" << (si.is_strict ? "<=" : "<") << " (+";

    for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
        const std::string& prog_var = lasso.program_vars[i];
        auto it = si.coefficients.find(prog_var);
        if (it != si.coefficients.end() && it->second != 0) {
            neg_si_xprime << " (* " << it->second << " " << lasso.loop.getSSAVar(prog_var, true) << ")";
        }
    }

    neg_si_xprime << " " << si.constant << ") 0)";
    solver->addAssertion(neg_si_xprime.str());
    
    bool sat = solver->checkSat();
    
    if (sat) {
        for (const auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in) {
            extractScalarValue(var_prog, ssa_in, lasso, solver, counterexample);
        }
        for (const auto& [var_prog, ssa_out] : lasso.loop.var_to_ssa_out) {
            extractScalarValue(var_prog, ssa_out, lasso, solver, counterexample);
        }
    }

    solver->pop();
    return !sat;
}

bool RankingAndInvariantValidator::checkSICompatibleWithLoop(
    const SupportingInvariant& si,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    std::map<std::string, double>& counterexample)
{
    solver->push();
    
    // Construire SI(x) avec les variables d'entrée du loop
    std::ostringstream si_x;
    si_x << "(" << (si.is_strict ? ">" : ">=") << " (+";
    
    for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
        const std::string& prog_var = lasso.program_vars[i];
        auto it = si.coefficients.find(prog_var);
        if (it != si.coefficients.end() && it->second != 0) {
            si_x << " (* " << it->second << " "
                 << lasso.loop.getSSAVar(prog_var, false) << ")";
        }
    }
    si_x << " " << si.constant << ") 0)";
    
    // Ajouter SI(x)
    solver->addAssertion(si_x.str());
    
    // Ajouter UNIQUEMENT le loop guard (pas les updates)
    // On veut vérifier : ∃x. loop_guard(x) ∧ SI(x)
    for (const auto& poly : lasso.loop.polyhedra) {
        for (const auto& ineq : poly) {
            // Ne prendre que les contraintes sur les variables d'entrée
            // (pas les contraintes d'égalité entre in et out)
            bool is_guard = true;
            for (const auto& [var_prog, ssa_out] : lasso.loop.var_to_ssa_out) {
                AffineTerm coef = ineq.getCoefficient(ssa_out);
                if ( !coef.isZero() ) {
                    is_guard = false;
                    break;
                }
            }
            
            if (is_guard) {
                std::string smt_constraint = ineq.toSMTLib2();
                solver->addAssertion(smt_constraint);
            }
        }
    }
    
    bool sat = solver->checkSat();
    
    if (!sat) {
        // SI incompatible avec loop guard
        // Pas de contre-exemple car UNSAT
    } else {
        for (const auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in) {
            extractScalarValue(var_prog, ssa_in, lasso, solver, counterexample);
        }
    }
    
    solver->pop();
    return sat;  // Valide si SAT (compatible)
}

// ============================================================================
// VÉRIFICATIONS - RANKING FUNCTION
// ============================================================================

bool RankingAndInvariantValidator::checkRFNonTriviality(
    const RankingFunction& rf) const
{
    for (const auto& [var, coef] : rf.coefficients) {
        if (coef != 0) {
            return true;
        }
    }
    return false;
}

bool RankingAndInvariantValidator::checkRFBounded(
    const RankingFunction& rf,
    const std::vector<SupportingInvariant>& supporting_invariants,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    std::map<std::string, double>& counterexample)
{
    solver->push();

    // Ajouter les contraintes du loop
    for (const auto& poly : lasso.loop.polyhedra) {
        for (const auto& ineq : poly) {
            solver->addAssertion(ineq.toSMTLib2());
        }
    }

    // Conditionner par les SI : SI(x) => f(x) >= 0
    for (const auto& si : supporting_invariants) {
        std::ostringstream si_formula;
        si_formula << "(";
        si_formula << (si.is_strict ? ">" : ">=");
        si_formula << " (+";
        for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
            const std::string& prog_var = lasso.program_vars[i];
            auto it = si.coefficients.find(prog_var);
            auto var_ssa_in = lasso.loop.var_to_ssa_in.find(prog_var);
            if (it != si.coefficients.end() && it->second != 0 &&
                var_ssa_in != lasso.loop.var_to_ssa_in.end()) {
                si_formula << " (* " << it->second << " " << var_ssa_in->second << ")";
            }
        }
        si_formula << " " << si.constant << ") 0)";
        solver->addAssertion(si_formula.str());
    }

    // Chercher f(x) < 0
    std::ostringstream f_negative;
    f_negative << "(< (+";

    for (const auto& [var, coef] : rf.coefficients) {
        if (coef != 0) {
            f_negative << " (* " << coef << " " << lasso.loop.getSSAVar(var, false) << ")";
        }
    }

    f_negative << " " << rf.constant << ") 0)";

    solver->addAssertion(f_negative.str());

    bool sat = solver->checkSat();
    
    if (sat) {
        for (const auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in) {
            extractScalarValue(var_prog, ssa_in, lasso, solver, counterexample);
        }
    }

    solver->pop();
    return !sat;
}

bool RankingAndInvariantValidator::checkRFDecreasing(
    const RankingFunction& rf,
    const std::vector<SupportingInvariant>& supporting_invariants,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver,
    double delta,
    std::map<std::string, double>& counterexample)
{
    solver->push();

    // Ajouter les contraintes du loop
    for (const auto& poly : lasso.loop.polyhedra) {
        for (const auto& ineq : poly) {
            solver->addAssertion(ineq.toSMTLib2());
        }
    }

    // La décroissance est conditionnée par les SI: SI(x) => f(x) - f(x') >= δ
    for (const auto& si : supporting_invariants) {
        std::ostringstream si_formula;
        si_formula << "(";
        si_formula << (si.is_strict ? ">" : ">=");
        si_formula << " (+";
        
        // Ajouter les termes des variables (utiliser in_vars du loop)
        for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
            const std::string& prog_var = lasso.program_vars[i];
            auto it = si.coefficients.find(prog_var);
            auto var_ssa_in = lasso.loop.var_to_ssa_in.find(prog_var);
            if (it != si.coefficients.end() && it->second != 0 &&
                var_ssa_in != lasso.loop.var_to_ssa_in.end()) {
                si_formula << " (* " << it->second << " " << var_ssa_in->second << ")";
            }
        }

        si_formula << " " << si.constant << ") 0)";
        solver->addAssertion(si_formula.str());
    }

    // Construire f(x) avec in_vars
    std::ostringstream f_x;
    f_x << "(+";
    
    for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
        const std::string& prog_var = lasso.program_vars[i];
        auto it = rf.coefficients.find(prog_var);
        auto var_ssa_in = lasso.loop.var_to_ssa_in.find(prog_var);
        if (it != rf.coefficients.end() && it->second != 0 && var_ssa_in != lasso.loop.var_to_ssa_in.end()) {
            // Utiliser in_vars du loop pour f(x)
            f_x << " (* " << it->second << " " << var_ssa_in->second << ")";
        }
    }

    f_x << " " << rf.constant << ")";
    
    // Construire f(x') avec out_vars
    std::ostringstream f_x_prime;
    f_x_prime << "(+";
    
    for (size_t i = 0; i < lasso.program_vars.size(); ++i) {
        const std::string& prog_var = lasso.program_vars[i];
        auto it = rf.coefficients.find(prog_var);
        auto var_ssa_out = lasso.loop.var_to_ssa_out.find(prog_var);
        if (it != rf.coefficients.end() && it->second != 0 && var_ssa_out != lasso.loop.var_to_ssa_out.end()) {
            // Utiliser out_vars du loop pour f(x')
            f_x_prime << " (* " << it->second << " " << var_ssa_out->second << ")";
        }
    }
    f_x_prime << " " << rf.constant << ")";

    // Chercher contre-exemple : f(x) - f(x') < δ
    std::ostringstream decrease_check;
    decrease_check << std::fixed << std::setprecision(6);
    decrease_check << "(< (- " << f_x.str() << " " << f_x_prime.str() << ") " << delta << ")";

    solver->addAssertion(decrease_check.str());

    bool sat = solver->checkSat();
    
    if (sat) {
        for (const auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in) {
            extractScalarValue(var_prog, ssa_in, lasso, solver, counterexample);
        }
        for (const auto& [var_prog, ssa_out] : lasso.loop.var_to_ssa_out) {
            extractScalarValue(var_prog, ssa_out, lasso, solver, counterexample);
        }
    }

    solver->pop();
    return !sat;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void RankingAndInvariantValidator::printValidationResult(const ValidationResult& result) const
{
    std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║    RÉSUMÉ DE VALIDATION                             ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    
    std::cout << "\n  Statut global : "
            << (result.is_valid ? "✅ VALIDE" : "❌ INVALIDE") << std::endl;
    
    std::cout << "\n  ┌─ Fonction de Ranking ─────────────┐" << std::endl;
    std::cout << "  │ Non-trivialité : "
            << (result.rf_non_trivial_check ? "✅" : "❌") << std::endl;
    std::cout << "  │ Bounded        : "
            << (result.rf_bounded_check ? "✅" : "❌") << std::endl;
    std::cout << "  │ Decreasing     : "
            << (result.rf_decreasing_check ? "✅" : "❌") << std::endl;
    std::cout << "  └────────────────────────────────────┘" << std::endl;
    
    std::cout << "\n  ┌─ Supporting Invariants ────────────┐" << std::endl;
    std::cout << "  │ Nombre total : " << result.si_results.size() << std::endl;
    std::cout << "  │ Tous valides : " << (result.all_si_valid ? "✅" : "❌") << std::endl;
    
    for (const auto& si_res : result.si_results) {
        std::cout << "  │ ─ SI #" << si_res.si_index << " : "
                << (si_res.is_valid ? "✅" : "❌");
        if (si_res.is_false_check) {
            std::cout << " (FAUX)";
        } else if (si_res.is_true_check) {
            std::cout << " (VRAI)";
        }
        std::cout << std::endl;
    }
    
    std::cout << "  └────────────────────────────────────┘" << std::endl;
    
    if (!result.is_valid && !result.error_message.empty()) {
        std::cout << "\n  !  Erreur : " << result.error_message << std::endl;
    }

    std::cout << std::endl;
}

// ============================================================================
// VALIDATION NESTED TEMPLATE
// ============================================================================

// Construit la formule SMT pour fi(x) avec les variables in du loop
static std::string buildRFFormula(
    const RankingFunction& rf,
    const LassoProgram& lasso,
    bool use_out_vars)
{
    std::ostringstream f;
    f << "(+";
    for (const auto& prog_var : lasso.program_vars) {
        auto coef_it = rf.coefficients.find(prog_var);
        if (coef_it == rf.coefficients.end() || coef_it->second == 0) continue;
        const auto& ssa_map = use_out_vars
            ? lasso.loop.var_to_ssa_out
            : lasso.loop.var_to_ssa_in;
        auto ssa_it = ssa_map.find(prog_var);
        if (ssa_it == ssa_map.end()) continue;
        f << " (* " << coef_it->second << " " << ssa_it->second << ")";
    }
    f << " " << rf.constant << ")";
    return f.str();
}

// Ajoute les contraintes du loop et des SI valides sur le solver (dans un push)
static void addLoopAndSIConstraints(
    const LassoProgram& lasso,
    const std::vector<SupportingInvariant>& valid_sis,
    std::shared_ptr<SMTSolver> solver)
{
    for (const auto& poly : lasso.loop.polyhedra) {
        for (const auto& ineq : poly) {
            solver->addAssertion(ineq.toSMTLib2());
        }
    }
    for (const auto& si : valid_sis) {
        std::ostringstream si_formula;
        si_formula << "(" << (si.is_strict ? ">" : ">=") << " (+";
        for (const auto& prog_var : lasso.program_vars) {
            auto coef_it = si.coefficients.find(prog_var);
            auto ssa_it = lasso.loop.var_to_ssa_in.find(prog_var);
            if (coef_it != si.coefficients.end() && coef_it->second != 0 &&
                ssa_it != lasso.loop.var_to_ssa_in.end()) {
                si_formula << " (* " << coef_it->second << " " << ssa_it->second << ")";
            }
        }
        si_formula << " " << si.constant << ") 0)";
        solver->addAssertion(si_formula.str());
    }
}

RankingAndInvariantValidator::NestedValidationResult RankingAndInvariantValidator::validateNested(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    std::shared_ptr<SMTSolver> solver)
{
    const auto& components = argument.components;
    const auto& supporting_invariants = argument.supporting_invariants;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    NestedValidationResult result;
    result.is_valid = false;
    result.all_si_valid = false;
    result.last_component_bounded_check = false;

    if (components.empty()) {
        result.error_message = "NestedTemplate: no components";
        return result;
    }

    if (verbose) {
        std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║    VALIDATION POST-SYNTHESE (NestedTemplate)         ║" << std::endl;
        std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;
    }

    registerProgramVariablesToSolver(solver, lasso);

    // ========================================================================
    // PARTIE 1 : VALIDATION DES SUPPORTING INVARIANTS
    // ========================================================================

    if (verbose) {
        std::cout << "\n╭─ Supporting Invariants ───────────────────────────────╮" << std::endl;
        std::cout << "  Nombre de SI : " << supporting_invariants.size() << std::endl;
    }

    // Reuse valid_sis member (reset first)
    valid_sis.clear();

    if (supporting_invariants.empty()) {
        result.all_si_valid = true;
    } else {
        int num_trivial_false = 0;
        int num_trivial_true = 0;
        int num_non_trivial = 0;
        int num_valid_non_trivial = 0;

        for (size_t i = 0; i < supporting_invariants.size(); ++i) {
            SIValidationResult si_result = validateSingleSI(i, supporting_invariants[i], lasso, solver);
            result.si_results.push_back(si_result);

            if (si_result.is_false_check) {
                num_trivial_false++;
            } else if (si_result.is_true_check) {
                num_trivial_true++;
            } else {
                num_non_trivial++;
                if (si_result.is_valid) {
                    num_valid_non_trivial++;
                    valid_sis.push_back(supporting_invariants[i]);
                }
            }
        }

        if (num_trivial_false > 0) {
            result.all_si_valid = false;
            result.error_message = "One or more SI are trivially false";
            if (verbose)
                std::cout << "  FAIL: " << num_trivial_false << " trivially false SI" << std::endl;
        } else {
            result.all_si_valid = true;
        }

        if (verbose) {
            std::cout << "    Trivial TRUE : " << num_trivial_true << std::endl;
            std::cout << "    Trivial FALSE: " << num_trivial_false << std::endl;
            std::cout << "    Non-trivial valid: " << num_valid_non_trivial
                      << "/" << num_non_trivial << std::endl;
        }
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;

    if (!result.all_si_valid)
        return result;

    // ========================================================================
    // PARTIE 2 : NON-TRIVIALITE DES COMPOSANTS
    // ========================================================================

    if (verbose)
        std::cout << "╭─ Components non-triviality ───────────────────────────╮" << std::endl;

    bool all_non_trivial = true;
    for (int i = 0; i < static_cast<int>(components.size()); ++i) {
        NestedValidationResult::ComponentResult cr;
        cr.index = i;
        cr.non_trivial_check = checkRFNonTriviality(components[i]);
        cr.nested_decrease_check = false;
        result.component_results.push_back(cr);
        if (!cr.non_trivial_check) {
            all_non_trivial = false;
            if (verbose)
                std::cout << "  FAIL: component f" << i << " is trivial (all-zero coefficients)" << std::endl;
        } else if (verbose) {
            std::cout << "  OK: component f" << i << " is non-trivial" << std::endl;
        }
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;

    if (!all_non_trivial) {
        result.error_message = "One or more nested components are trivial";
        return result;
    }

    // ========================================================================
    // PARTIE 3 : CONDITION DE DECROISSANCE NESTED
    //
    //   i=0 : f0(x) - f0(x') >= delta   (cherche contre-exemple: diff < delta)
    //   i>0 : fi(x) - fi(x') + f_{i-1}(x) >= 0  (contre-exemple: sum < 0)
    // ========================================================================

    if (verbose)
        std::cout << "╭─ Nested decrease conditions ──────────────────────────╮" << std::endl;

    double delta = components[0].delta;
    bool all_decrease_ok = true;

    for (int i = 0; i < static_cast<int>(components.size()); ++i) {
        solver->push();
        addLoopAndSIConstraints(lasso, valid_sis, solver);

        std::string fi_x  = buildRFFormula(components[i], lasso, false);
        std::string fi_xp = buildRFFormula(components[i], lasso, true);

        std::string decrease_formula;
        if (i == 0) {
            // f0(x) - f0(x') < delta
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6);
            oss << "(< (- " << fi_x << " " << fi_xp << ") " << delta << ")";
            decrease_formula = oss.str();
        } else {
            // fi(x) - fi(x') + f_{i-1}(x) < 0
            std::string fi_prev_x = buildRFFormula(components[i - 1], lasso, false);
            decrease_formula = "(< (+ (- " + fi_x + " " + fi_xp + ") " + fi_prev_x + ") 0)";
        }

        solver->addAssertion(decrease_formula);
        bool sat = solver->checkSat();

        if (sat) {
            for (const auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in)
                extractScalarValue(var_prog, ssa_in, lasso, solver, result.component_results[i].counterexample);
            for (const auto& [var_prog, ssa_out] : lasso.loop.var_to_ssa_out)
                extractScalarValue(var_prog, ssa_out, lasso, solver, result.component_results[i].counterexample);
        }

        solver->pop();

        result.component_results[i].nested_decrease_check = !sat;
        if (sat) {
            all_decrease_ok = false;
            if (verbose) {
                if (i == 0)
                    std::cout << "  FAIL: f0(x) - f0(x') >= " << delta << " violated" << std::endl;
                else
                    std::cout << "  FAIL: f" << i << "(x) - f" << i
                              << "(x') + f" << (i-1) << "(x) >= 0 violated" << std::endl;
            }
        } else if (verbose) {
            if (i == 0)
                std::cout << "  OK: f0(x) - f0(x') >= " << delta << std::endl;
            else
                std::cout << "  OK: f" << i << "(x) - f" << i
                          << "(x') + f" << (i-1) << "(x) >= 0" << std::endl;
        }
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;

    if (!all_decrease_ok) {
        result.error_message = "Nested decrease condition violated";
        return result;
    }

    // ========================================================================
    // PARTIE 4 : BORNE — f_{k-1}(x) >= 0
    // ========================================================================

    if (verbose)
        std::cout << "╭─ Bounded: f_{k-1}(x) >= 0 ───────────────────────────╮" << std::endl;

    {
        solver->push();
        addLoopAndSIConstraints(lasso, valid_sis, solver);

        const RankingFunction& last = components.back();
        std::string flast_x = buildRFFormula(last, lasso, false);
        // cherche contre-exemple: f_{k-1}(x) < 0
        solver->addAssertion("(< " + flast_x + " 0)");
        bool sat = solver->checkSat();

        if (sat) {
            for (const auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in)
                extractScalarValue(var_prog, ssa_in, lasso, solver, result.bounded_counterexample);
        }

        solver->pop();
        result.last_component_bounded_check = !sat;

        if (verbose) {
            int k = static_cast<int>(components.size());
            std::cout << "  " << (result.last_component_bounded_check ? "OK" : "FAIL")
                      << ": f" << (k-1) << "(x) >= 0"
                      << (result.last_component_bounded_check ? "" : " violated") << std::endl;
        }
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────────────────╯\n" << std::endl;

    if (!result.last_component_bounded_check) {
        result.error_message = "Last nested component is not bounded (f_{k-1}(x) < 0 possible)";
        return result;
    }

    result.is_valid = true;
    return result;
}

// ============================================================================
// AFFICHAGE NESTED
// ============================================================================

void RankingAndInvariantValidator::printNestedValidationResult(
    const NestedValidationResult& result) const
{
    std::cout << "\n╔═══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║    NESTED VALIDATION SUMMARY                         ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════╝" << std::endl;

    std::cout << "\n  Global status : "
              << (result.is_valid ? "OK VALID" : "FAIL INVALID") << std::endl;

    std::cout << "\n  SI valid : " << (result.all_si_valid ? "OK" : "FAIL") << std::endl;
    for (const auto& si_res : result.si_results) {
        std::cout << "    SI #" << si_res.si_index << " : "
                  << (si_res.is_valid ? "OK" : "FAIL");
        if (si_res.is_false_check)       std::cout << " (trivially false)";
        else if (si_res.is_true_check)   std::cout << " (trivially true)";
        std::cout << std::endl;
    }

    std::cout << "\n  Components :" << std::endl;
    for (const auto& cr : result.component_results) {
        std::cout << "    f" << cr.index
                  << "  non-trivial=" << (cr.non_trivial_check ? "OK" : "FAIL")
                  << "  decrease=" << (cr.nested_decrease_check ? "OK" : "FAIL")
                  << std::endl;
    }
    std::cout << "  Bounded (last component) : "
              << (result.last_component_bounded_check ? "OK" : "FAIL") << std::endl;

    if (!result.is_valid && !result.error_message.empty())
        std::cout << "\n  Error: " << result.error_message << std::endl;

    std::cout << std::endl;
}