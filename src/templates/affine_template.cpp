#include <iostream>
#include <sstream>

#include "templates/affine_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;
// ============================================================================
// CONSTRUCTEUR
// ============================================================================

AffineTemplate::AffineTemplate(int num_si_strict, int num_si_nonstrict, int delta_value)
    : num_strict_invariants_(num_si_strict)
    , num_nonstrict_invariants_(num_si_nonstrict)
    , num_supporting_invariants_(num_si_strict + num_si_nonstrict)
    , delta_value_(delta_value)
    , initialized_(false)
    , delta_param_("DELTA")
{
    if(VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║            AFFINE TEMPLATE (BMS)           ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Strict SI:     " << num_strict_invariants_ << std::endl;
        std::cout << "  Non-strict SI: " << num_nonstrict_invariants_ << std::endl;
        std::cout << "  Total SI:      " << num_supporting_invariants_ << std::endl;
    }
}

// ============================================================================
// IMPLÉMENTATION DE L'INTERFACE RankingTemplate
// ============================================================================

void AffineTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    initializeParameters();
    initialized_ = true;
    if(VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  Variables:     " << lasso_.program_vars.size() << std::endl;
        std::cout << "  Parameters:    " << (ranking_params_.size() +
                                            si_params_.size() * (lasso_.program_vars.size() + 1))
                    << std::endl;
    }
}

std::vector<RankingTemplate::MotzkinContext> AffineTemplate::getConstraints() const {
    if (!initialized_) {
        throw std::runtime_error("AffineTemplate::getConstraints() called before init()");
    }
    
    std::vector<MotzkinContext> all_contexts;
    
    if(VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "\n┌─ Generating BMS Constraints ─┐" << std::endl;
    
    // φ1: Stem Initiation
    if (!lasso_.hasNoStem()) {
        auto phi1 = generatePhi1_StemInitiation();
        if(VERBOSITY == VerbosityLevel::VERBOSE)
            std::cout << "│ φ1: Stem Initiation : " << phi1.size() << std::endl;
        all_contexts.insert(all_contexts.end(), phi1.begin(), phi1.end());
    } else {
        if(VERBOSITY == VerbosityLevel::VERBOSE)
            std::cout << "│ φ1: (skipped - no stem)" << std::endl;
    }
    
    // φ2: Loop Consecution
    auto phi2 = generatePhi2_LoopConsecution();
    if(VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "│ φ2: Loop Consecution : " << phi2.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi2.begin(), phi2.end());
    
    // φ3: Loop Decrement
    auto phi3 = generatePhi3_RankingDecrement();
    if(VERBOSITY == VerbosityLevel::VERBOSE) 
        std::cout << "│ φ3: Loop Decrement : " << phi3.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi3.begin(), phi3.end());
    
    // φ4: Loop Boundedness
    auto phi4 = generatePhi4_RankingBoundedness();
    if(VERBOSITY == VerbosityLevel::VERBOSE) 
        std::cout << "│ φ4: Loop Boundedness : " << phi4.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi4.begin(), phi4.end());
    
    if(VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "└───────────────────────────────┘" << std::endl;
        std::cout << "  Total Motzkin contexts: " << all_contexts.size() << std::endl;
    }
    return all_contexts;
}

RankingTemplate::TemplateParameters AffineTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("getParameters() called before init()");
    }
    
    TemplateParameters params;
    params.ranking_params = ranking_params_;
    params.delta_param = delta_param_;
    params.delta_value = delta_value_;
    
    // Aplatir les SI params en un seul vecteur
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

void AffineTemplate::printInfo() const {
    if(VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n┌─ Template Info ─────────┐" << std::endl;
        std::cout << "│ Name: " << getName() << std::endl;
        std::cout << "│ Description: " << getDescription() << std::endl;
        std::cout << "│ Strict SI: " << num_strict_invariants_ << std::endl;
        std::cout << "│ Non-strict SI: " << num_nonstrict_invariants_ << std::endl;
        std::cout << "│ Total SI: " << num_supporting_invariants_ << std::endl;
        if (initialized_) {
            std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
            std::cout << "│ Total params: " << getParameters().getTotalParameterCount() << std::endl;
        }
        std::cout << "└─────────────────────────┘" << std::endl;
    }
}

// ============================================================================
// INITIALISATION DES PARAMÈTRES
// ============================================================================

void AffineTemplate::initializeParameters() {
    int n = lasso_.program_vars.size();
    
    // Paramètres de la ranking function: c₀, c₁, ..., cₙ
    ranking_params_.clear();
    // n + 1 paramètres : un pour chaque variable + une constante
    for (int i = 0; i < n; ++i) {
        ranking_params_.push_back("RANKING_C_" + std::to_string(i));
    }
    ranking_params_.push_back("RANKING_C_const");
    
    // Paramètres des supporting invariants
    si_params_.clear();
    // Chaque SI a n + 1 paramètres : un pour chaque variable + une constante
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
// CONSTRUCTION DES TERMES
// ============================================================================

LinearInequality AffineTemplate::buildRankingFunction(
    const std::vector<std::string>& vars) const
{
    LinearInequality result;
    result.strict = false;
    result.motzkin_coef = LinearInequality::ANYTHING;
    
    // Pour chaque variable: cᵢ·varᵢ
    for (size_t i = 0; i < vars.size(); ++i) {
        AffineTerm coef;
        coef.coefficients[ranking_params_[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }
    
    // Constante: c_const
    AffineTerm const_term;
    const_term.coefficients[ranking_params_.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;
    
    return result;
}

LinearInequality AffineTemplate::buildSupportingInvariant(
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
AffineTemplate::generatePhi1_StemInitiation() const {
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
            // if (!is_strict) {
            //     neg_si.constant = neg_si.constant + AffineTerm(-1.0);
            // }
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
AffineTemplate::generatePhi2_LoopConsecution() const {
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

            // Construire variables x et x'
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
// φ3: RANKING DECREMENT - loop(x,x') ∧ Σ SI(x) ≥ 0 → f(x) - f(x') ≥ δ
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
AffineTemplate::generatePhi3_RankingDecrement() const {
    std::vector<MotzkinContext> contexts;

    // Pour chaque polyèdre du loop
    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ3: Ranking decrement (poly " + std::to_string(poly_idx) + ")";

        // Ajouter les contraintes du loop
        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        // Construire variables x et x'
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

        LinearInequality f_x = buildRankingFunction(loop_in_vars);
        LinearInequality f_x_prime = buildRankingFunction(loop_out_vars);

        // Conclusion inversée : ¬(f(x) - f(x') ≥ δ) = -f(x) + f(x') + δ > 0
        LinearInequality neg_decrement;
        neg_decrement.strict = true;
        neg_decrement.motzkin_coef = LinearInequality::ONE;

        // -f(x)
        for (size_t i = 0; i < loop_in_vars.size(); ++i) {
            AffineTerm coef = f_x.getCoefficient(loop_in_vars[i]);
            coef.negate();
            neg_decrement.setCoefficient(loop_in_vars[i], coef);
        }

        // +f(x')
        for (size_t i = 0; i < loop_out_vars.size(); ++i) {
            AffineTerm coef = f_x_prime.getCoefficient(loop_out_vars[i]);
            neg_decrement.setCoefficient(loop_out_vars[i], coef);
        }

        // Constante : -f_const + f'_const + δ
        neg_decrement.constant = neg_decrement.constant - f_x.constant + f_x_prime.constant;
        neg_decrement.constant.coefficients[delta_param_] = 1;  // +δ

        ctx.constraints.push_back(neg_decrement);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// φ4: RANKING BOUNDEDNESS - loop(x,x') ∧ Σ SI(x) ≥ 0 → f(x) ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
AffineTemplate::generatePhi4_RankingBoundedness() const {
    std::vector<MotzkinContext> contexts;
    
    // Pour chaque polyèdre du loop
    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ4: Ranking boundedness (poly " + std::to_string(poly_idx) + ")";
        
        // Ajouter les contraintes du loop
        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }
        
        // Construire variables x
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

        // Ajouter ¬(f(x) ≥ 0) = -f(x) > 0
        LinearInequality neg_f_x = buildRankingFunction(loop_in_vars);
        neg_f_x.strict = true;
        neg_f_x.negate();
        // neg_f_x.constant = neg_f_x.constant + AffineTerm(-1.0);
        neg_f_x.motzkin_coef = LinearInequality::ONE;
        
        ctx.constraints.push_back(neg_f_x);
        contexts.push_back(ctx);
        poly_idx++;
    }
    
    return contexts;
}