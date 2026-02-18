#include <iostream>
#include <sstream>

#include "templates/affine_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

AffineTemplate::AffineTemplate(int delta_value)
    : delta_value_(delta_value)
    , initialized_(false)
    , delta_param_("DELTA")
{
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║            AFFINE TEMPLATE                 ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
    }
}

// ============================================================================
// IMPLÉMENTATION DE L'INTERFACE RankingTemplate
// ============================================================================

void AffineTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    initializeParameters();
    initialized_ = true;
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  Variables:  " << lasso_.program_vars.size() << std::endl;
        std::cout << "  RF params:  " << ranking_params_.size() << std::endl;
    }
}

std::vector<RankingTemplate::MotzkinContext> AffineTemplate::getConstraints(
    const std::vector<LinearInequality>& si_preconditions) const
{
    if (!initialized_) {
        throw std::runtime_error("AffineTemplate::getConstraints() called before init()");
    }

    std::vector<MotzkinContext> all_contexts;

    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "\n┌─ Generating Affine RF Constraints ─┐" << std::endl;

    // φ3: Loop Decrement
    auto phi3 = generatePhi3_RankingDecrement(si_preconditions);
    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "│ φ3: Loop Decrement   : " << phi3.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi3.begin(), phi3.end());

    // φ4: Loop Boundedness
    auto phi4 = generatePhi4_RankingBoundedness(si_preconditions);
    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "│ φ4: Loop Boundedness : " << phi4.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi4.begin(), phi4.end());

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "└────────────────────────────────────┘" << std::endl;
        std::cout << "  Total RF contexts: " << all_contexts.size() << std::endl;
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
    return params;
}

void AffineTemplate::printInfo() const {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n┌─ Template Info ──────────┐" << std::endl;
        std::cout << "│ Name: " << getName() << std::endl;
        std::cout << "│ Description: " << getDescription() << std::endl;
        if (initialized_) {
            std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
            std::cout << "│ RF params: " << ranking_params_.size() << std::endl;
        }
        std::cout << "└──────────────────────────┘" << std::endl;
    }
}

// ============================================================================
// INITIALISATION DES PARAMÈTRES
// ============================================================================

void AffineTemplate::initializeParameters() {
    int n = static_cast<int>(lasso_.program_vars.size());

    ranking_params_.clear();
    for (int i = 0; i < n; ++i) {
        ranking_params_.push_back("RANKING_C_" + std::to_string(i));
    }
    ranking_params_.push_back("RANKING_C_const");
}

// ============================================================================
// CONSTRUCTION DE LA RANKING FUNCTION
// ============================================================================

LinearInequality AffineTemplate::buildRankingFunction(
    const std::vector<std::string>& vars) const
{
    LinearInequality result;
    result.strict = false;
    result.motzkin_coef = LinearInequality::ANYTHING;

    for (size_t i = 0; i < vars.size(); ++i) {
        AffineTerm coef;
        coef.coefficients[ranking_params_[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }

    AffineTerm const_term;
    const_term.coefficients[ranking_params_.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;

    return result;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::vector<RankingFunction> AffineTemplate::extractRankingFunctions(
    std::shared_ptr<SMTSolver> solver,
    const std::vector<std::string>& program_vars) const
{
    RankingFunction rf;
    size_t n = program_vars.size();

    for (size_t i = 0; i < n && i < ranking_params_.size() - 1; ++i) {
        rf.coefficients[program_vars[i]] = solver->getValue(ranking_params_[i]);
    }
    if (!ranking_params_.empty()) {
        rf.constant = solver->getValue(ranking_params_.back());
    }
    rf.delta = solver->getValue(delta_param_);

    return {rf};
}

// ============================================================================
// φ3: RANKING DECREMENT — loop(x,x') ∧ Σ SI(x) ≥ 0 → f(x) - f(x') ≥ δ
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
AffineTemplate::generatePhi3_RankingDecrement(
    const std::vector<LinearInequality>& si_preconditions) const
{
    std::vector<MotzkinContext> contexts;

    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ3: Ranking decrement (poly " + std::to_string(poly_idx) + ")";

        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        std::vector<std::string> loop_in_vars, loop_out_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        }

        // Prémisses SI : SI(x) ≥ 0 (injectées de l'extérieur)
        for (const auto& si_precond : si_preconditions) {
            ctx.constraints.push_back(si_precond);
        }

        LinearInequality f_x = buildRankingFunction(loop_in_vars);
        LinearInequality f_x_prime = buildRankingFunction(loop_out_vars);

        // Conclusion inversée : ¬(f(x) - f(x') ≥ δ) = -f(x) + f(x') + δ > 0
        LinearInequality neg_decrement;
        neg_decrement.strict = true;
        neg_decrement.motzkin_coef = LinearInequality::ONE;

        for (size_t i = 0; i < loop_in_vars.size(); ++i) {
            AffineTerm coef = f_x.getCoefficient(loop_in_vars[i]);
            coef.negate();
            neg_decrement.setCoefficient(loop_in_vars[i], coef);
        }
        for (size_t i = 0; i < loop_out_vars.size(); ++i) {
            AffineTerm coef = f_x_prime.getCoefficient(loop_out_vars[i]);
            neg_decrement.setCoefficient(loop_out_vars[i], coef);
        }

        neg_decrement.constant = neg_decrement.constant - f_x.constant + f_x_prime.constant;
        neg_decrement.constant.coefficients[delta_param_] = 1;

        ctx.constraints.push_back(neg_decrement);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// φ4: RANKING BOUNDEDNESS — loop(x,x') ∧ Σ SI(x) ≥ 0 → f(x) ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
AffineTemplate::generatePhi4_RankingBoundedness(
    const std::vector<LinearInequality>& si_preconditions) const
{
    std::vector<MotzkinContext> contexts;

    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ4: Ranking boundedness (poly " + std::to_string(poly_idx) + ")";

        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        std::vector<std::string> loop_in_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        }

        // Prémisses SI : SI(x) ≥ 0 (injectées de l'extérieur)
        for (const auto& si_precond : si_preconditions) {
            ctx.constraints.push_back(si_precond);
        }

        // Conclusion inversée : ¬(f(x) ≥ 0) = -f(x) > 0
        LinearInequality neg_f_x = buildRankingFunction(loop_in_vars);
        neg_f_x.strict = true;
        neg_f_x.negate();
        neg_f_x.motzkin_coef = LinearInequality::ONE;

        ctx.constraints.push_back(neg_f_x);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}
