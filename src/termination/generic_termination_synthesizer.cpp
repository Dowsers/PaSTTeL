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
    std::shared_ptr<SMTSolver> solver)
    : lasso_(lasso)
    , template_(template_ptr)
    , solver_(solver)
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
        // Étape 1: Initialiser le template
        if (verbose)
            std::cout << "\n[1/5] Initializing template..." << std::endl;
        template_->init(lasso_);
        if (verbose)
            template_->printInfo();
        
        // Étape 2: Déclarer les paramètres SMT
        if (verbose) 
            std::cout << "\n[2/5] Declaring SMT parameters..." << std::endl;
        auto params = template_->getParameters();
        declareParameters(params);
        
        // Étape 3: Ajouter les contraintes de non-trivialité
        // if (verbose) 
        //     std::cout << "\n[3/5] Adding non-triviality constraints..." << std::endl;
        // addNonTrivialityConstraints(params);
        
        // Étape 4: Appliquer les transformations de Motzkin
        if (verbose) 
            std::cout << "\n[4/5] Applying Motzkin transformations..." << std::endl;
        auto contexts = template_->getConstraints();
        applyMotzkinTransformations(contexts);
        
        // Étape 5: Résoudre avec le solveur SMT
        if (verbose) {
            std::cout << "\n[5/5] Solving with SMT..." << std::endl;
            std::cout << "  Total assertions: " << solver_->getAssertionCount() << std::endl;
        }

        result.is_valid = solver_->checkSat();

        // Extraire les paramètres du modèle si SAT
        if (result.is_valid) {
            result.parameters = extractParameters(params);
            // Paramètres nulles → échec de synthèse (solution triviale)
            if (result.parameters.empty()) {
                result.is_valid = false;
                if (verbose)
                    std::cout << "  ⚠ Warning: Null coefficients extracted from the model" << std::endl;
            } else {
                if (verbose)
                    std::cout << "\n✓ SAT - Termination argument found!" << std::endl;
                extractResults(params);  // Extrait RankingFunction et SI
                synthesized_ = true;
                last_result_ = result;
            }
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
// DÉCLARATION DES PARAMÈTRES
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
        
    // Déclarer les paramètres de SI
    for (const auto& param : params.si_params) {
        solver_->declareVariable(param, "Real");
    }

    if (verbose) 
        std::cout << "  SI params: " << params.si_params.size() << std::endl;
    
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
        std::cout << "  Total parameters: " << params.getTotalParameterCount() << std::endl;
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
    
    // Construire la liste des variables à éliminer (x et x')
    std::vector<std::string> all_vars;

    for (const auto& [var_prog, ssa_in] : lasso_.loop.var_to_ssa_in) {
        all_vars.push_back(ssa_in);
    }
    for (const auto& [var_prog, ssa_out] : lasso_.loop.var_to_ssa_out) {
        all_vars.push_back(ssa_out);
    }

    // Ajouter les variables d'abstraction de fonctions (uf__f__0, uf__keccak256__1, etc.)
    // Ces variables apparaissent dans les polyèdres et doivent être éliminées par Motzkin
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
    int context_idx = 0;
    for (const auto& ctx : contexts) {
        MotzkinTransformation motzkin;
        motzkin.addConstraintsToSolver(
            ctx.constraints,
            all_vars,
            solver_,
            ctx.annotation);
        context_idx++;
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
    
    // Extraire les paramètres de SI
    for (const auto& param : params.si_params) {
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
    // Échelle pour gérer les décimales (10^6)
    const long long SCALE = 1000000;
    std::vector<long long> int_coeffs;
    
    // Convertir coefficients en entiers
    for (double c : coefficients) {
        if (std::abs(c) > 1e-9) {
            long long int_val = static_cast<long long>(std::round(c * SCALE));
            if (int_val != 0) {
                int_coeffs.push_back(std::abs(int_val));
            }
        }
    }
    
    // Ajouter constante
    if (std::abs(constant) > 1e-9) {
        long long int_const = static_cast<long long>(std::round(constant * SCALE));
        if (int_const != 0) {
            int_coeffs.push_back(std::abs(int_const));
        }
    }
    
    // Cas où tous les coefficients sont nuls
    if (int_coeffs.empty()) {
        return 1;
    }
    
    // Calcul du GCD cumulatif
    long long result = int_coeffs[0];
    for (size_t i = 1; i < int_coeffs.size(); ++i) {
        result = gcd(result, int_coeffs[i]);
        if (result == 1) break;  // Optimisation
    }
    
    // Diviser par SCALE et arrondir
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
    
    // Normaliser coefficients des variables
    for (auto& [var, coef] : rf.coefficients) {
        coef /= divisor;
    }
    
    // Normaliser constante
    rf.constant /= divisor;
    
    // Normaliser delta
    rf.delta /= divisor;
}

void GenericTerminationSynthesizer::normalizeSupportingInvariant(
    SupportingInvariant& si,
    long long gcd_value) const
{
    if (gcd_value <= 1) return;
    
    double divisor = static_cast<double>(gcd_value);
    
    // Normaliser coefficients
    for (auto& [var, coef] : si.coefficients) {
        coef /= divisor;
    }
    
    // Normaliser constante
    si.constant /= divisor;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS STRUCTURÉS
// ============================================================================

void GenericTerminationSynthesizer::extractResults(
    const RankingTemplate::TemplateParameters& params)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n╭─ Extraction des résultats ────────────────╮" << std::endl;
    
    // ========================================================================
    // PARTIE 1: EXTRACTION BRUTE - RANKING FUNCTION
    // ========================================================================
    
    ranking_function_.coefficients.clear();
    std::vector<double> raw_coefficients;
    
    size_t num_vars = lasso_.program_vars.size();
    
    // Extraire coefficients des variables
    for (size_t i = 0; i < num_vars && i < params.ranking_params.size() - 1; ++i) {
        double coef = solver_->getValue(params.ranking_params[i]);
        ranking_function_.coefficients[lasso_.program_vars[i]] = coef;
        raw_coefficients.push_back(coef);
    }
    
    // Extraire constante
    double raw_constant = 0.0;
    if (!params.ranking_params.empty()) {
        raw_constant = solver_->getValue(params.ranking_params.back());
        ranking_function_.constant = raw_constant;
    }
    
    // Extraire delta
    if (!params.delta_param.empty()) {
        ranking_function_.delta = solver_->getValue(params.delta_param);
    }
    
    if (verbose)
        std::cout << "  ✓ Coefficients bruts extraits" << std::endl;
    
    // ========================================================================
    // PARTIE 2: NORMALISATION GCD - RANKING FUNCTION
    // ========================================================================
    
    long long rf_gcd = computeGCD(raw_coefficients, raw_constant);
    
    if (verbose) {
        std::cout << "\n   Normalisation GCD:" << std::endl;
        std::cout << "    GCD(RF) = " << rf_gcd << std::endl;
    }
    
    if (rf_gcd > 1) {
        if (verbose){
            std::cout << "    Avant: f(x) = ";
            bool first = true;
            for (const auto& [var, coef] : ranking_function_.coefficients) {
                if (std::abs(coef) > 1e-9) {
                    if (!first && coef > 0) std::cout << " + ";
                    if (coef < 0) std::cout << " - ";
                    std::cout << std::abs(coef) << "·" << var;
                    first = false;
                }
            }
            std::cout << " + " << ranking_function_.constant << std::endl;
        }

        normalizeRankingFunction(ranking_function_, rf_gcd);
        
        if (verbose){
            std::cout << "    Après: f(x) = ";
            bool first = true;
            for (const auto& [var, coef] : ranking_function_.coefficients) {
                if (std::abs(coef) > 1e-9) {
                    if (!first && coef > 0) std::cout << " + ";
                    if (coef < 0) std::cout << " - ";
                    std::cout << std::abs(coef) << "·" << var;
                }
                first = false;
            }
            std::cout << " + " << ranking_function_.constant << std::endl;
        }
        
    } else {
        if (verbose)
            std::cout << "    Aucune normalisation nécessaire" << std::endl;
    }
    
    // ========================================================================
    // PARTIE 3: EXTRACTION BRUTE - SUPPORTING INVARIANTS
    // ========================================================================
    
    supporting_invariants_.clear();
    
    int num_si = template_->getNumSupportingInvariants();
    int params_per_si = num_vars + 1;
    
    if (verbose)
        std::cout << "\n   Supporting Invariants: " << num_si << std::endl;
    
    for (int si_idx = 0; si_idx < num_si; ++si_idx) {
        SupportingInvariant si;
        
        // Déterminer strict/non-strict
        if (si_idx < static_cast<int>(params.si_is_strict.size())) {
            si.is_strict = params.si_is_strict[si_idx];
        } else {
            si.is_strict = false;
        }
        
        // Extraire coefficients bruts
        std::vector<double> si_raw_coeffs;
        int start_idx = si_idx * params_per_si;
        
        for (size_t i = 0; i < num_vars && start_idx + i < params.si_params.size(); ++i) {
            double coef = solver_->getValue(params.si_params[start_idx + i]);
            si.coefficients[lasso_.program_vars[i]] = coef;
            si_raw_coeffs.push_back(coef);
        }
        
        // Constante brute
        double si_raw_const = 0.0;
        if (start_idx + num_vars < params.si_params.size()) {
            si_raw_const = solver_->getValue(params.si_params[start_idx + num_vars]);
            si.constant = si_raw_const;
        }
        
        // ====================================================================
        // NORMALISATION GCD - CE SI
        // ====================================================================
        
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
        supporting_invariants_.push_back(si);
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────╯\n" << std::endl;
}

// ============================================================================
// ACCESSEURS
// ============================================================================

const GenericTerminationSynthesizer::RankingFunction&
GenericTerminationSynthesizer::getRankingFunction() const {
    if (!synthesized_ || !last_result_.is_valid) {
        throw std::runtime_error("getRankingFunction() called but synthesis was not successful");
    }
    return ranking_function_;
}

const std::vector<GenericTerminationSynthesizer::SupportingInvariant>&
GenericTerminationSynthesizer::getSupportingInvariants() const {
    if (!synthesized_ || !last_result_.is_valid) {
        throw std::runtime_error("getSupportingInvariants() called but synthesis was not successful");
    }
    return supporting_invariants_;
}

// ============================================================================
// MÉTHODES toString()
// ============================================================================

std::string GenericTerminationSynthesizer::RankingFunction::toString(
    const std::vector<std::string>& vars) const
{
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& var : vars) {
        auto it = coefficients.find(var);
        if (it != coefficients.end() && std::abs(it->second) > 1e-9) {
            if (!first && it->second > 0) {
                oss << " + ";
            } else if (it->second < 0) {
                oss << " - ";
            }
            
            double abs_coef = std::abs(it->second);
            if (std::abs(abs_coef - 1.0) > 1e-9) {
                oss << abs_coef << "·";
            }
            oss << var;
            first = false;
        }
    }
    
    if (std::abs(constant) > 1e-9) {
        if (!first && constant > 0) {
            oss << " + ";
        } else if (constant < 0) {
            oss << " - ";
        }
        oss << std::abs(constant);
    } else if (first) {
        oss << "0";
    }
    
    return oss.str();
}

std::string GenericTerminationSynthesizer::SupportingInvariant::toString(
    const std::vector<std::string>& vars) const
{
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& var : vars) {
        auto it = coefficients.find(var);
        if (it != coefficients.end() && std::abs(it->second) > 1e-9) {
            if (!first && it->second > 0) {
                oss << " + ";
            } else if (it->second < 0) {
                oss << " - ";
            }
            
            double abs_coef = std::abs(it->second);
            if (std::abs(abs_coef - 1.0) > 1e-9) {
                oss << abs_coef << "·";
            }
            oss << var;
            first = false;
        }
    }
    
    if (std::abs(constant) > 1e-9) {
        if (!first && constant > 0) {
            oss << " + ";
        } else if (constant < 0) {
            oss << " - ";
        }
        oss << std::abs(constant);
    } else if (first) {
        oss << "0";
    }
    
    return oss.str();
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

    // Afficher la ranking function
    if (synthesized_ && !ranking_function_.coefficients.empty()) {
        std::cout << "\n  Ranking Function:" << std::endl;
        std::cout << "  f(x) = " << ranking_function_.toString(lasso_.program_vars) << std::endl;
        std::cout << "  δ = " << ranking_function_.delta << std::endl;
    }
    
    // Afficher les supporting invariants
    if (synthesized_ && !supporting_invariants_.empty()) {
        std::cout << "\n Supporting Invariants:" << std::endl;
        for (size_t i = 0; i < supporting_invariants_.size(); ++i) {
            const auto& si = supporting_invariants_[i];
            std::cout << "  SI_" << i << ": " << si.toString(lasso_.program_vars);
            std::cout << " " << (si.is_strict ? ">" : ">=") << " 0";
            std::cout << " (" << (si.is_strict ? "strict" : "non-strict") << ")" << std::endl;
        }
    }
}