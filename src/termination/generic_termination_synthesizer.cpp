#include <iostream>
#include <sstream>
#include <cmath>

#include "termination/generic_termination_synthesizer.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

GenericTerminationSynthesizer::GenericTerminationSynthesizer(
    const LassoProgram& lasso,
    RankingTemplate* template_ptr,
    std::shared_ptr<SMTSolver> solver,
    int num_si_strict,
    int num_si_nonstrict)
    : lasso_(lasso)
    , template_(template_ptr)
    , solver_(solver)
    , sig_(num_si_strict, num_si_nonstrict)
    , synthesized_(false)
{
    if (!template_) {
        throw std::runtime_error("GenericTerminationSynthesizer: null template pointer");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  GENERIC TERMINATION SYNTHESIZER           ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Template: " << template_->getName() << std::endl;
        std::cout << "  Description: " << template_->getDescription() << std::endl;
        std::cout << "  SI strict:     " << num_si_strict << std::endl;
        std::cout << "  SI non-strict: " << num_si_nonstrict << std::endl;
    }
}

// ============================================================================
// SYNTHÈSE PRINCIPALE
// ============================================================================

GenericTerminationSynthesizer::SynthesisResult GenericTerminationSynthesizer::synthesize() {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose) {
        std::cout << "\n┌─────────────────────────────────────────┐" << std::endl;
        std::cout << "│ Starting Synthesis                      │" << std::endl;
        std::cout << "└─────────────────────────────────────────┘" << std::endl;
    }

    SynthesisResult result;
    result.template_name = template_->getName();
    result.description = template_->getDescription();

    try {
        // Étape 1: Initialiser le template et le SIG
        if (verbose)
            std::cout << "\n[1/4] Initializing template and SIG..." << std::endl;
        template_->init(lasso_);
        sig_.init(lasso_);
        if (verbose)
            template_->printInfo();

        // Étape 2: Déclarer les paramètres SMT
        if (verbose)
            std::cout << "\n[2/4] Declaring SMT parameters..." << std::endl;
        auto params = template_->getParameters();
        declareParameters(params);
        sig_.declareParameters(solver_);

        // Étape 3: Appliquer les transformations de Motzkin
        if (verbose)
            std::cout << "\n[3/4] Applying Motzkin transformations..." << std::endl;

        // Construire les prémisses SI pour les templates RF
        std::vector<std::string> loop_in_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        }
        auto si_preconditions = sig_.buildPreconditions(loop_in_vars);

        // Collecter tous les contextes Motzkin
        std::vector<RankingTemplate::MotzkinContext> all_contexts;

        auto phi1 = sig_.generatePhi1();
        auto phi2 = sig_.generatePhi2();
        auto rf_contexts = template_->getConstraints(si_preconditions);

        if (verbose) {
            std::cout << "  φ1 (SI initiation):   " << phi1.size() << std::endl;
            std::cout << "  φ2 (SI consecution):  " << phi2.size() << std::endl;
            std::cout << "  RF contexts:          " << rf_contexts.size() << std::endl;
        }

        all_contexts.insert(all_contexts.end(), phi1.begin(), phi1.end());
        all_contexts.insert(all_contexts.end(), phi2.begin(), phi2.end());
        all_contexts.insert(all_contexts.end(), rf_contexts.begin(), rf_contexts.end());

        applyMotzkinTransformations(all_contexts);

        // Étape 4: Résoudre avec le solveur SMT
        if (verbose) {
            std::cout << "\n[4/4] Solving with SMT..." << std::endl;
            std::cout << "  Total assertions: " << solver_->getAssertionCount() << std::endl;
        }

        result.is_valid = solver_->checkSat();

        // Extraire les paramètres du modèle si SAT
        if (result.is_valid) {
            result.parameters = extractParameters(params);
            // Paramètres nuls → échec de synthèse (solution triviale)
            // if (result.parameters.empty()) {
            //     result.is_valid = false;
            //     if (verbose)
            //         std::cout << "  ⚠ Warning: Null coefficients extracted from the model" << std::endl;
            // } else {
                if (verbose)
                    std::cout << "\n✓ SAT - Termination argument found!" << std::endl;
                extractResults();  // Extrait RankingFunction et SI
                synthesized_ = true;
                last_result_ = result;
            // }
        } else {
            if (verbose)
                std::cout << "\n✗ UNSAT - No termination argument exists" << std::endl;
        }

    } catch (const std::exception& e) {
        if (verbose)
            std::cerr << "\n  Error during synthesis: " << e.what() << std::endl;
        result.is_valid = false;
    }

    return result;
}

// ============================================================================
// DÉCLARATION DES PARAMÈTRES RF
// ============================================================================

// TODO: should we add an option on the sort (Int, Real) ? - for now we assume Real
void GenericTerminationSynthesizer::declareParameters(
    const RankingTemplate::TemplateParameters& params)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Déclarer les paramètres de ranking
    for (const auto& param : params.ranking_params) {
        solver_->declareVariable(param, "Real");
    }
    if (verbose)
        std::cout << "  Ranking params: " << params.ranking_params.size() << std::endl;

    // Déclarer delta si présent
    if (!params.delta_param.empty()) {
        solver_->declareVariable(params.delta_param, "Real");

        // Contrainte: δ ≥ 1 empêchant d'avoir des solutions triviales avec RANKING_C_i = 0 et δ = 0
        std::ostringstream delta_constraint;
        delta_constraint << "(>= " << params.delta_param << " " << params.delta_value << ")";
        solver_->addAssertion(delta_constraint.str());

        if (verbose)
            std::cout << "  Delta: " << params.delta_param << " (≥ " << params.delta_value << ")" << std::endl;
    }

    if (verbose)
        std::cout << "  Total RF parameters: " << params.getTotalParameterCount() << std::endl;
}

// ============================================================================
// CONTRAINTES DE NON-TRIVIALITÉ
// ============================================================================

// TODO: inutile ?
void GenericTerminationSynthesizer::addNonTrivialityConstraints(
    const RankingTemplate::TemplateParameters& params)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Au moins un coefficient de ranking non-nul
    if (params.ranking_params.empty()) {
        if (verbose)
            std::cout << "  ⚠ No ranking parameters - skipping non-triviality constraint" << std::endl;
        return;
    }

    std::ostringstream constraint;
    constraint << "(or";

    for (const auto& param : params.ranking_params) {
        constraint << " (not (= " << param << " 0.0))";
    }

    constraint << ")";
    solver_->addAssertion(constraint.str());

    if (verbose)
        std::cout << "  ✓ Ranking function non-triviality constraint added" << std::endl;
}

// ============================================================================
// APPLICATION DES TRANSFORMATIONS DE MOTZKIN
// ============================================================================

void GenericTerminationSynthesizer::applyMotzkinTransformations(
    const std::vector<RankingTemplate::MotzkinContext>& contexts)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "  Motzkin contexts: " << contexts.size() << std::endl;

    // Construire la liste des variables à éliminer (x et x' — loop et stem)
    std::vector<std::string> all_vars;

    for (const auto& [var_prog, ssa_in] : lasso_.loop.var_to_ssa_in) {
        all_vars.push_back(ssa_in);
    }
    for (const auto& [var_prog, ssa_out] : lasso_.loop.var_to_ssa_out) {
        all_vars.push_back(ssa_out);
    }
    for (const auto& [var_prog, ssa_in] : lasso_.stem.var_to_ssa_in) {
        all_vars.push_back(ssa_in);
    }
    for (const auto& [var_prog, ssa_out] : lasso_.stem.var_to_ssa_out) {
        all_vars.push_back(ssa_out);
    }

    // Ajouter les variables d'abstraction de fonctions (uf__f__0, uf__keccak256__1, etc.)
    for (const auto& abs : lasso_.function_abstractions) {
        all_vars.push_back(abs.fresh_var);
    }

    // Debug: afficher quelques contextes
    if (verbose) {
        std::cout << "\n  First few contexts:" << std::endl;
        int display_count = std::min(5, (int)contexts.size());
        for (int i = 0; i < display_count; ++i) {
            std::cout << "  [" << i << "] " << contexts[i].annotation << std::endl;
            std::cout << "      Constraints: " << contexts[i].constraints.size() << std::endl;
        }
        if (contexts.size() > 5) {
            std::cout << "  ... and " << (contexts.size() - 5) << " more" << std::endl;
        }
    }

    // Appliquer Motzkin sur chaque contexte
    for (const auto& ctx : contexts) {
        MotzkinTransformation motzkin;
        motzkin.addConstraintsToSolver(
            ctx.constraints,
            all_vars,
            solver_,
            ctx.annotation);
    }

    // Réinitialiser le compteur Motzkin pour être déterministe
    MotzkinTransformation::init_counter();

    if (verbose)
        std::cout << "  ✓ Applied " << contexts.size() << " Motzkin transformations" << std::endl;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::map<std::string, double> GenericTerminationSynthesizer::extractParameters(
    const RankingTemplate::TemplateParameters& params)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n┌─ Extracting Solution ─┐" << std::endl;

    std::map<std::string, double> values;
    bool has_non_zero_coeff_param = false;

    // Extraire les paramètres de ranking
    for (const auto& param : params.ranking_params) {
        double value = solver_->getValue(param);
        values[param] = value;
        has_non_zero_coeff_param = has_non_zero_coeff_param || (std::abs(value) > 1e-9);
        if (verbose)
            std::cout << "│ " << param << " = " << value << std::endl;
    }

    if (!has_non_zero_coeff_param) {
        return {};  // Tous les paramètres sont nuls - retourner une map vide
    }

    // Extraire les paramètres de SI (depuis SIG)
    for (const auto& param : sig_.getSIParams()) {
        double value = solver_->getValue(param);
        values[param] = value;
        if (verbose)
            std::cout << "│ " << param << " = " << value << std::endl;
    }

    // Extraire delta si présent
    if (!params.delta_param.empty()) {
        double value = solver_->getValue(params.delta_param);
        values[params.delta_param] = value;
        if (verbose)
            std::cout << "│ " << params.delta_param << " = " << value << std::endl;
    }

    if (verbose)
        std::cout << "└────────────────────────┘" << std::endl;

    return values;
}

// ========================================================================
// CALCUL DU GCD
// ========================================================================

long long GenericTerminationSynthesizer::gcd(long long a, long long b) const {
    a = std::abs(a);
    b = std::abs(b);
    while (b != 0) {
        long long temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

long long GenericTerminationSynthesizer::computeGCD(
    const std::vector<double>& coefficients,
    double constant) const
{
    const long long SCALE = 1000000;
    std::vector<long long> int_coeffs;

    for (double c : coefficients) {
        if (std::abs(c) > 1e-9) {
            long long int_val = static_cast<long long>(std::round(c * SCALE));
            if (int_val != 0) {
                int_coeffs.push_back(std::abs(int_val));
            }
        }
    }

    if (std::abs(constant) > 1e-9) {
        long long int_const = static_cast<long long>(std::round(constant * SCALE));
        if (int_const != 0) {
            int_coeffs.push_back(std::abs(int_const));
        }
    }

    if (int_coeffs.empty()) {
        return 1;
    }

    long long result = int_coeffs[0];
    for (size_t i = 1; i < int_coeffs.size(); ++i) {
        result = gcd(result, int_coeffs[i]);
        if (result == 1) break;
    }

    result = result / SCALE;
    return (result == 0) ? 1 : result;
}

// ============================================================================
// NORMALISATION
// ============================================================================

void GenericTerminationSynthesizer::normalizeRankingFunction(
    RankingFunction& rf,
    long long gcd_value) const
{
    if (gcd_value <= 1) return;

    double divisor = static_cast<double>(gcd_value);

    for (auto& [var, coef] : rf.coefficients) {
        coef /= divisor;
    }
    rf.constant /= divisor;
    rf.delta /= divisor;
}

void GenericTerminationSynthesizer::normalizeSupportingInvariant(
    SupportingInvariant& si,
    long long gcd_value) const
{
    if (gcd_value <= 1) return;

    double divisor = static_cast<double>(gcd_value);

    for (auto& [var, coef] : si.coefficients) {
        coef /= divisor;
    }
    si.constant /= divisor;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS STRUCTURÉS
// ============================================================================

void GenericTerminationSynthesizer::extractResults()
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n╭─ Extraction des résultats ────────────────╮" << std::endl;

    size_t num_vars = lasso_.program_vars.size();

    // ========================================================================
    // PARTIE 1: DÉLÉGATION AU TEMPLATE — chaque template sait extraire ses composantes
    // ========================================================================

    termination_argument_.components =
        template_->extractRankingFunctions(solver_, lasso_.program_vars);

    if (verbose)
        std::cout << "  ✓ Composantes extraites : " << termination_argument_.components.size() << std::endl;

    // ========================================================================
    // PARTIE 2: NORMALISATION GCD — une passe par composante
    // ========================================================================

    if (verbose)
        std::cout << "\n   Normalisation GCD par composante:" << std::endl;

    for (size_t ci = 0; ci < termination_argument_.components.size(); ++ci) {
        auto& rf = termination_argument_.components[ci];
        std::vector<double> raw_coeffs;
        for (const auto& [var, coef] : rf.coefficients) {
            raw_coeffs.push_back(coef);
        }
        long long g = computeGCD(raw_coeffs, rf.constant);
        if (verbose)
            std::cout << "    f" << ci << ": GCD = " << g;
        if (g > 1) {
            normalizeRankingFunction(rf, g);
            if (verbose) std::cout << " (normalisé)";
        }
        if (verbose)
            std::cout << " → " << rf.toString(lasso_.program_vars) << std::endl;
    }

    // Composante primaire : rétrocompatibilité avec ranking_function
    if (!termination_argument_.components.empty()) {
        termination_argument_.ranking_function = termination_argument_.components[0];
    }

    // ========================================================================
    // PARTIE 3: EXTRACTION BRUTE - SUPPORTING INVARIANTS (via SIG)
    // ========================================================================

    termination_argument_.supporting_invariants.clear();

    int num_si = sig_.getNumSI();
    auto si_is_strict = sig_.getSIIsStrict();
    auto si_params = sig_.getSIParams();
    int params_per_si = static_cast<int>(num_vars) + 1;

    if (verbose)
        std::cout << "\n   Supporting Invariants: " << num_si << std::endl;

    for (int si_idx = 0; si_idx < num_si; ++si_idx) {
        SupportingInvariant si;

        if (si_idx < static_cast<int>(si_is_strict.size())) {
            si.is_strict = si_is_strict[si_idx];
        } else {
            si.is_strict = false;
        }

        std::vector<double> si_raw_coeffs;
        int start_idx = si_idx * params_per_si;

        for (size_t i = 0; i < num_vars && start_idx + (int)i < (int)si_params.size(); ++i) {
            double coef = solver_->getValue(si_params[start_idx + i]);
            si.coefficients[lasso_.program_vars[i]] = coef;
            si_raw_coeffs.push_back(coef);
        }

        double si_raw_const = 0.0;
        if (start_idx + (int)num_vars < (int)si_params.size()) {
            si_raw_const = solver_->getValue(si_params[start_idx + num_vars]);
            si.constant = si_raw_const;
        }

        long long si_gcd = computeGCD(si_raw_coeffs, si_raw_const);

        if (verbose)
            std::cout << "    SI_" << si_idx << ": GCD = " << si_gcd;

        if (si_gcd > 1) {
            normalizeSupportingInvariant(si, si_gcd);
            if (verbose)
                std::cout << " (normalisé)";
        }

        if (verbose) {
            std::cout << " → " << si.toString(lasso_.program_vars);
            std::cout << " " << (si.is_strict ? ">" : ">=") << " 0" << std::endl;
        }
        termination_argument_.supporting_invariants.push_back(si);
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────╯\n" << std::endl;
}

// ============================================================================
// ACCESSEURS
// ============================================================================

const TerminationArgument&
GenericTerminationSynthesizer::getTerminationArgument() const {
    if (!synthesized_ || !last_result_.is_valid) {
        throw std::runtime_error("getTerminationArgument() called but synthesis was not successful");
    }
    return termination_argument_;
}

// ============================================================================
// AFFICHAGE DES RÉSULTATS
// ============================================================================

void GenericTerminationSynthesizer::printResults(const SynthesisResult& result) const {
    if (!result.is_valid) {
        std::cout << "\n❌ No termination argument found (UNSAT)" << std::endl;
        std::cout << "  Template: " << result.template_name << std::endl;
        return;
    }

    std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TERMINATION ARGUMENT                      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════╝" << std::endl;

    std::cout << "\n  Template: " << result.template_name << std::endl;
    std::cout << "   " << result.description << std::endl;

    if (synthesized_ && !termination_argument_.components.empty()) {
        std::cout << "\n  Ranking Function(s):" << std::endl;
        bool multi = termination_argument_.components.size() > 1;
        for (size_t ci = 0; ci < termination_argument_.components.size(); ++ci) {
            const auto& rf = termination_argument_.components[ci];
            if (multi)
                std::cout << "  f" << ci << "(x) = ";
            else
                std::cout << "  f(x) = ";
            std::cout << rf.toString(lasso_.program_vars) << std::endl;
            std::cout << "  δ" << (multi ? std::to_string(ci) : "") << " = " << rf.delta << std::endl;
        }
    }

    if (synthesized_ && !termination_argument_.supporting_invariants.empty()) {
        std::cout << "\n Supporting Invariants:" << std::endl;
        for (size_t i = 0; i < termination_argument_.supporting_invariants.size(); ++i) {
            const auto& si = termination_argument_.supporting_invariants[i];
            std::cout << "  SI_" << i << ": " << si.toString(lasso_.program_vars);
            std::cout << " " << (si.is_strict ? ">" : ">=") << " 0";
            std::cout << " (" << (si.is_strict ? "strict" : "non-strict") << ")" << std::endl;
        }
    }
}
