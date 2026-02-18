#include <iostream>
#include <sstream>

#include "templates/nested_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

NestedTemplate::NestedTemplate(int num_components, int num_si_strict, int num_si_nonstrict, int delta_value)
    : num_components_(num_components)
    , num_strict_invariants_(num_si_strict)
    , num_nonstrict_invariants_(num_si_nonstrict)
    , num_supporting_invariants_(num_si_strict + num_si_nonstrict)
    , initialized_(false)
    , delta_param_("DELTA")
    , delta_value_(delta_value)
{
    if (num_components_ < 2) {
        throw std::invalid_argument("NestedTemplate requires at least 2 components");
    }
    
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  NESTED TEMPLATE WITH SI (BMS INTEGRATED)  ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Components:    " << num_components_ << std::endl;
        std::cout << "  Strict SI:     " << num_strict_invariants_ << std::endl;
        std::cout << "  Non-strict SI: " << num_nonstrict_invariants_ << std::endl;
        std::cout << "  Total SI:      " << num_supporting_invariants_ << std::endl;
    }
}
// ============================================================================
// INITIALISATION
// ============================================================================

void NestedTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    initializeParameters();
    initialized_ = true;
    
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  Variables:     " << lasso_.program_vars.size() << std::endl;
        std::cout << "  Parameters:    " << getParameters().getTotalParameterCount() << std::endl;
    }
}

void NestedTemplate::initializeParameters() {
    int n = lasso_.program_vars.size();
    
    // Paramètres pour chaque composante fᵢ
    component_params_.clear();
    for (int i = 0; i < num_components_; ++i) {
        std::vector<std::string> comp_params;
        for (int j = 0; j < n; ++j) {
            comp_params.push_back("RANK_" + std::to_string(i) + "_" + std::to_string(j));
        }
        comp_params.push_back("RANK_" + std::to_string(i) + "_const");
        component_params_.push_back(comp_params);
    }
    
    // Paramètres des supporting invariants
    si_params_.clear();
    for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
        std::vector<std::string> si_coeffs;
        for (int i = 0; i < n; ++i) {
            si_coeffs.push_back("SUP_INVAR_" + std::to_string(si_idx) + "_" + std::to_string(i));
        }
        si_coeffs.push_back("SUP_INVAR_" + std::to_string(si_idx) + "_const");
        si_params_.push_back(si_coeffs);
    }
}

// ============================================================================
// IMPLÉMENTATION DE L'INTERFACE
// ============================================================================

std::vector<RankingTemplate::MotzkinContext> NestedTemplate::getConstraints() const {
    if (!initialized_) {
        throw std::runtime_error("NestedTemplate::getConstraints() called before init()");
    }
    
    std::vector<MotzkinContext> all_contexts;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    
    if (verbose)
        std::cout << "\n┌─ Generating Nested + BMS Constraints ─┐" << std::endl;
    
    // φ1: Stem Initiation (SI)
    if (!lasso_.hasNoStem()) {
        auto phi1 = generatePhi1_StemInitiation();
        if (verbose)
            std::cout << "│ φ1: Stem Initiation (SI) : " << phi1.size() << std::endl;
        all_contexts.insert(all_contexts.end(), phi1.begin(), phi1.end());
    } else {
        if (verbose)
            std::cout << "│ φ1: (skipped - no stem)" << std::endl;
    }
    
    // φ2: Loop Consecution (SI)
    auto phi2 = generatePhi2_LoopConsecution();
    if (verbose)
        std::cout << "│ φ2: Loop Consecution (SI) : " << phi2.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi2.begin(), phi2.end());
    
    // φ0: f₀ decrement with SI
    auto phi0 = generatePhi0_Decrement0();
    if (verbose)
        std::cout << "│ φ0: f₀ decrement : " << phi0.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi0.begin(), phi0.end());
    
    // φᵢ: fᵢ borrowing with SI (i > 0)
    for (int i = 1; i < num_components_; ++i) {
        auto phii = generatePhiI_DecrementI(i);
        if (verbose)
            std::cout << "│ φ" << i << ": f" << i << " borrowing : " << phii.size() << std::endl;
        all_contexts.insert(all_contexts.end(), phii.begin(), phii.end());
    }
    
    // φₙ: Boundedness
    auto phin = generatePhiN_Boundedness();
    if (verbose)
        std::cout << "│ φₙ: Boundedness : " << phin.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phin.begin(), phin.end());
    
    if (verbose) {
        std::cout << "└────────────────────────────────────────┘" << std::endl;
        std::cout << "  Total contexts: " << all_contexts.size() << std::endl;
    }
    return all_contexts;
}

RankingTemplate::TemplateParameters NestedTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("getParameters() called before init()");
    }
    
    TemplateParameters params;
    
    // Tous les paramètres des composantes
    for (const auto& comp_params : component_params_) {
        for (const auto& param : comp_params) {
            params.ranking_params.push_back(param);
        }
    }
    
    params.delta_param = delta_param_;
    params.delta_value = delta_value_;
    
    // Aplatir les SI params
    for (const auto& si_param_set : si_params_) {
        for (const auto& param : si_param_set) {
            params.si_params.push_back(param);
        }
    }
    
    // Remplir si_is_strict pour chaque SI
    for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
        bool is_strict = (si_idx < num_strict_invariants_);
        params.si_is_strict.push_back(is_strict);
    }
    
    return params;
}

std::string NestedTemplate::getDescription() const {
    std::ostringstream oss;
    oss << num_components_ << "-nested with " << num_supporting_invariants_ << " SI: ";
    oss << "f₀ decreases, each fᵢ can 'borrow' from fᵢ₋₁";
    return oss.str();
}

void NestedTemplate::printInfo() const {
    std::cout << "\n┌─ Template Info ─────────┐" << std::endl;
    std::cout << "│ Name: " << getName() << std::endl;
    std::cout << "│ Components: " << num_components_ << std::endl;
    std::cout << "│ Strict SI: " << num_strict_invariants_ << std::endl;
    std::cout << "│ Non-strict SI: " << num_nonstrict_invariants_ << std::endl;
    std::cout << "│ Total SI: " << num_supporting_invariants_ << std::endl;
    if (initialized_) {
        std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
        std::cout << "│ Total params: " << getParameters().getTotalParameterCount() << std::endl;
    }
    std::cout << "└─────────────────────────┘" << std::endl;
}

// ============================================================================
// CONSTRUCTION DES TERMES
// ============================================================================

LinearInequality NestedTemplate::buildComponent(
    int idx, 
    const std::vector<std::string>& vars) const
{
    LinearInequality result;
    result.strict = false;
    result.motzkin_coef = LinearInequality::ANYTHING;
    
    const auto& comp_params = component_params_[idx];
    
    // Pour chaque variable: cᵢ·varᵢ
    for (size_t i = 0; i < vars.size(); ++i) {
        AffineTerm coef;
        coef.coefficients[comp_params[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }
    
    // Constante: c_const
    AffineTerm const_term;
    const_term.coefficients[comp_params.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;
    
    return result;
}

LinearInequality NestedTemplate::buildSupportingInvariant(
    int si_index,
    const std::vector<std::string>& vars,
    bool is_strict) const
{
    LinearInequality result;
    result.strict = is_strict;
    result.motzkin_coef = LinearInequality::ANYTHING;
    
    const auto& si_coeffs = si_params_[si_index];
    
    // Pour chaque variable: sᵢ·varᵢ
    for (size_t i = 0; i < vars.size(); ++i) {
        AffineTerm coef;
        coef.coefficients[si_coeffs[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }
    
    // Constante: s_const
    AffineTerm const_term;
    const_term.coefficients[si_coeffs.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;
    
    return result;
}

// ============================================================================
// φ1: STEM INITIATION - stem(x,x') → SI(x') ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext> 
NestedTemplate::generatePhi1_StemInitiation() const {
    std::vector<MotzkinContext> contexts;
    
    // Pour chaque SI
    for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
        bool is_strict = (si_idx < num_strict_invariants_);
        
        // Pour chaque polyèdre du stem
        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.stem.polyhedra) {
            MotzkinContext ctx;
            ctx.annotation = "φ1: SI_" + std::to_string(si_idx) + 
                           " initiation (poly " + std::to_string(poly_idx) + ")";
            
            // Ajouter les contraintes du stem
            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }
            
            // Construire les variables de sortie du stem (x')
            std::vector<std::string> stem_out_vars;
            for (const auto& var : lasso_.program_vars) {
                stem_out_vars.push_back(lasso_.stem.getSSAVar(var, true));
            }
            
            // Ajouter ¬(SI(x') ≥ 0) = -SI(x') > 0
            LinearInequality neg_si = buildSupportingInvariant(si_idx, stem_out_vars, is_strict);
            neg_si.negate();
            neg_si.strict = !is_strict;
            neg_si.motzkin_coef = LinearInequality::ONE;
            
            ctx.constraints.push_back(neg_si);
            contexts.push_back(ctx);
            poly_idx++;
        }
    }
    
    return contexts;
}

// ============================================================================
// φ2: LOOP CONSECUTION - loop(x,x') ∧ SI(x) ≥ 0 → SI(x') ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhi2_LoopConsecution() const {
    std::vector<MotzkinContext> contexts;

    // Pour chaque SI
    for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
        bool is_strict = (si_idx < num_strict_invariants_);

        // Pour chaque polyèdre du loop
        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            MotzkinContext ctx;
            ctx.annotation = "φ2: SI_" + std::to_string(si_idx) +
                           " consecution (poly " + std::to_string(poly_idx) + ")";

            // Ajouter les contraintes du loop
            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            // Construire les variables d'entrée et sortie du loop
            std::vector<std::string> loop_in_vars, loop_out_vars;
            for (const auto& var : lasso_.program_vars) {
                loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
                loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
            }

            // Précondition BMS : SI(x) ≥ 0 (hypothèse Motzkin)
            LinearInequality si_precond = buildSupportingInvariant(si_idx, loop_in_vars, is_strict);
            ctx.constraints.push_back(si_precond);

            // Conclusion inversée : ¬(SI(x') ≥ 0) = -SI(x') > 0
            LinearInequality neg_si_prime = buildSupportingInvariant(si_idx, loop_out_vars, is_strict);
            neg_si_prime.negate();
            neg_si_prime.strict = !is_strict;
            neg_si_prime.motzkin_coef = LinearInequality::ONE;
            ctx.constraints.push_back(neg_si_prime);

            contexts.push_back(ctx);
            poly_idx++;
        }
    }

    return contexts;
}

// ============================================================================
// φ0: LOOP DECREMENT f₀ - loop(x,x') ∧ Σ SI(x) ≥ 0 → f₀(x) - f₀(x') ≥ δ
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhi0_Decrement0() const {
    std::vector<MotzkinContext> contexts;

    // Pour chaque polyèdre du loop
    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ0: f₀ decrement with SI (poly " + std::to_string(poly_idx) + ")";

        // Ajouter les contraintes du loop
        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        // Construire les variables d'entrée et sortie
        std::vector<std::string> loop_in_vars, loop_out_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        }

        // Préconditions BMS : SI(x) ≥ 0 pour chaque SI (hypothèses Motzkin)
        for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
            bool is_strict = (si_idx < num_strict_invariants_);
            LinearInequality si_precond = buildSupportingInvariant(si_idx, loop_in_vars, is_strict);
            ctx.constraints.push_back(si_precond);
        }

        LinearInequality f0_in = buildComponent(0, loop_in_vars);
        LinearInequality f0_out = buildComponent(0, loop_out_vars);

        // Conclusion inversée : ¬(f₀(x) - f₀(x') ≥ δ) = -f₀(x) + f₀(x') + δ > 0
        LinearInequality neg_decrement;
        neg_decrement.strict = true;
        neg_decrement.motzkin_coef = LinearInequality::ONE;

        // -f₀(x)
        for (size_t i = 0; i < loop_in_vars.size(); ++i) {
            AffineTerm coef = f0_in.getCoefficient(loop_in_vars[i]);
            coef.negate();
            neg_decrement.setCoefficient(loop_in_vars[i], coef);
        }
        // +f₀(x')
        for (size_t i = 0; i < loop_out_vars.size(); ++i) {
            AffineTerm coef = f0_out.getCoefficient(loop_out_vars[i]);
            neg_decrement.setCoefficient(loop_out_vars[i], coef);
        }

        // Constante : -f_const + f'_const + δ
        neg_decrement.constant = neg_decrement.constant - f0_in.constant + f0_out.constant;
        neg_decrement.constant.coefficients[delta_param_] = 1;  // +δ

        ctx.constraints.push_back(neg_decrement);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// φᵢ: LOOP DECREMENT fᵢ - loop(x,x') ∧ Σ SI(x) ≥ 0 → fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) > 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhiI_DecrementI(int i) const {
    std::vector<MotzkinContext> contexts;

    // Pour chaque polyèdre du loop
    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ" + std::to_string(i) + ": f" + std::to_string(i) +
                        " borrowing with SI (poly " + std::to_string(poly_idx) + ")";

        // Ajouter les contraintes du loop
        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        // Construire les variables
        std::vector<std::string> loop_in_vars, loop_out_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        }

        // Préconditions BMS : SI(x) ≥ 0 pour chaque SI (hypothèses Motzkin)
        for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
            bool is_strict = (si_idx < num_strict_invariants_);
            LinearInequality si_precond = buildSupportingInvariant(si_idx, loop_in_vars, is_strict);
            ctx.constraints.push_back(si_precond);
        }

        LinearInequality fi_in = buildComponent(i, loop_in_vars);
        LinearInequality fi_out = buildComponent(i, loop_out_vars);
        LinearInequality fi_minus_1 = buildComponent(i - 1, loop_in_vars);

        // Conclusion inversée : ¬(fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) > 0) = fᵢ(x') - fᵢ(x) - fᵢ₋₁(x) ≥ 0
        LinearInequality neg_decrement;
        neg_decrement.strict = false;
        neg_decrement.motzkin_coef = LinearInequality::ONE;

        // -fᵢ(x) + fᵢ(x')
        for (size_t j = 0; j < loop_in_vars.size(); ++j) {
            AffineTerm coef_in = fi_in.getCoefficient(loop_in_vars[j]);
            coef_in.negate();
            AffineTerm coef_out = fi_out.getCoefficient(loop_out_vars[j]);
            neg_decrement.setCoefficient(loop_in_vars[j], coef_in);
            neg_decrement.setCoefficient(loop_out_vars[j], coef_out);
        }

        // -fᵢ₋₁(x)
        for (size_t j = 0; j < loop_in_vars.size(); ++j) {
            AffineTerm coef = fi_minus_1.getCoefficient(loop_in_vars[j]);
            coef.negate();
            AffineTerm current = neg_decrement.getCoefficient(loop_in_vars[j]);
            neg_decrement.setCoefficient(loop_in_vars[j], current + coef);
        }

        // Constante : -fi_const + fi'_const - fi-1_const
        neg_decrement.constant = neg_decrement.constant - fi_in.constant + fi_out.constant - fi_minus_1.constant;

        ctx.constraints.push_back(neg_decrement);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// φₙ: BOUNDEDNESS - loop(x,x') ∧ Σ SI(x) ≥ 0 → fₙ₋₁(x) ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhiN_Boundedness() const {
    std::vector<MotzkinContext> contexts;

    // Pour chaque polyèdre du loop
    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φₙ: Boundedness (poly " + std::to_string(poly_idx) + ")";

        // Ajouter les contraintes du loop
        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        // Construire les variables d'entrée
        std::vector<std::string> loop_in_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        }

        // Préconditions BMS : SI(x) ≥ 0 pour chaque SI (hypothèses Motzkin)
        for (int si_idx = 0; si_idx < num_supporting_invariants_; ++si_idx) {
            bool is_strict = (si_idx < num_strict_invariants_);
            LinearInequality si_precond = buildSupportingInvariant(si_idx, loop_in_vars, is_strict);
            ctx.constraints.push_back(si_precond);
        }

        // Conclusion inversée : ¬(fₙ₋₁(x) ≥ 0) = -fₙ₋₁(x) > 0
        LinearInequality fn_minus_1 = buildComponent(num_components_ - 1, loop_in_vars);

        LinearInequality neg_boundedness;
        neg_boundedness.strict = true;
        neg_boundedness.motzkin_coef = LinearInequality::ONE;

        // -fₙ₋₁(x)
        for (size_t i = 0; i < loop_in_vars.size(); ++i) {
            AffineTerm coef = fn_minus_1.getCoefficient(loop_in_vars[i]);
            coef.negate();
            neg_boundedness.setCoefficient(loop_in_vars[i], coef);
        }

        // Constante: -fn-1_const
        neg_boundedness.constant = fn_minus_1.constant;
        neg_boundedness.constant.negate();

        ctx.constraints.push_back(neg_boundedness);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}