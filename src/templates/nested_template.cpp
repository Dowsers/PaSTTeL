#include <iostream>
#include <sstream>

#include "templates/nested_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

NestedTemplate::NestedTemplate(int num_components, int delta_value)
    : num_components_(num_components)
    , delta_value_(delta_value)
    , initialized_(false)
    , delta_param_("DELTA")
{
    if (num_components_ < 2) {
        throw std::invalid_argument("NestedTemplate requires at least 2 components");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  NESTED TEMPLATE                           ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Components: " << num_components_ << std::endl;
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
        std::cout << "  Variables:  " << lasso_.program_vars.size() << std::endl;
        std::cout << "  RF params:  " << getParameters().getTotalParameterCount() << std::endl;
    }
}

void NestedTemplate::initializeParameters() {
    int n = static_cast<int>(lasso_.program_vars.size());

    component_params_.clear();
    for (int i = 0; i < num_components_; ++i) {
        std::vector<std::string> comp_params;
        for (int j = 0; j < n; ++j) {
            comp_params.push_back("RANK_" + std::to_string(i) + "_" + std::to_string(j));
        }
        comp_params.push_back("RANK_" + std::to_string(i) + "_const");
        component_params_.push_back(comp_params);
    }
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::vector<RankingFunction> NestedTemplate::extractRankingFunctions(
    std::shared_ptr<SMTSolver> solver,
    const std::vector<std::string>& program_vars) const
{
    std::vector<RankingFunction> components;
    size_t n = program_vars.size();
    double delta = solver->getValue(delta_param_);

    for (int i = 0; i < num_components_; ++i) {
        RankingFunction rf;
        for (size_t j = 0; j < n && j < component_params_[i].size() - 1; ++j) {
            rf.coefficients[program_vars[j]] = solver->getValue(component_params_[i][j]);
        }
        if (!component_params_[i].empty()) {
            rf.constant = solver->getValue(component_params_[i].back());
        }
        rf.delta = delta;
        components.push_back(rf);
    }
    return components;
}

// ============================================================================
// IMPLÉMENTATION DE L'INTERFACE
// ============================================================================

std::vector<RankingTemplate::MotzkinContext> NestedTemplate::getConstraints(
    const std::vector<LinearInequality>& si_preconditions) const
{
    if (!initialized_) {
        throw std::runtime_error("NestedTemplate::getConstraints() called before init()");
    }

    std::vector<MotzkinContext> all_contexts;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n┌─ Generating Nested RF Constraints ─┐" << std::endl;

    // φ0: f₀ decrement
    auto phi0 = generatePhi0_Decrement0(si_preconditions);
    if (verbose)
        std::cout << "│ φ0: f₀ decrement : " << phi0.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phi0.begin(), phi0.end());

    // φᵢ: fᵢ borrowing (i > 0)
    for (int i = 1; i < num_components_; ++i) {
        auto phii = generatePhiI_DecrementI(i, si_preconditions);
        if (verbose)
            std::cout << "│ φ" << i << ": f" << i << " borrowing : " << phii.size() << std::endl;
        all_contexts.insert(all_contexts.end(), phii.begin(), phii.end());
    }

    // φₙ: Boundedness
    auto phin = generatePhiN_Boundedness(si_preconditions);
    if (verbose)
        std::cout << "│ φₙ: Boundedness : " << phin.size() << std::endl;
    all_contexts.insert(all_contexts.end(), phin.begin(), phin.end());

    if (verbose) {
        std::cout << "└────────────────────────────────────┘" << std::endl;
        std::cout << "  Total RF contexts: " << all_contexts.size() << std::endl;
    }
    return all_contexts;
}

RankingTemplate::TemplateParameters NestedTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("getParameters() called before init()");
    }

    TemplateParameters params;

    for (const auto& comp_params : component_params_) {
        for (const auto& param : comp_params) {
            params.ranking_params.push_back(param);
        }
    }

    params.delta_param = delta_param_;
    params.delta_value = delta_value_;

    return params;
}

std::string NestedTemplate::getDescription() const {
    std::ostringstream oss;
    oss << num_components_ << "-nested: f₀ decreases, each fᵢ can 'borrow' from fᵢ₋₁";
    return oss.str();
}

void NestedTemplate::printInfo() const {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n┌─ Template Info ──────────┐" << std::endl;
        std::cout << "│ Name: " << getName() << std::endl;
        std::cout << "│ Components: " << num_components_ << std::endl;
        if (initialized_) {
            std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
            std::cout << "│ RF params: " << getParameters().getTotalParameterCount() << std::endl;
        }
        std::cout << "└──────────────────────────┘" << std::endl;
    }
}

// ============================================================================
// CONSTRUCTION DES COMPOSANTES
// ============================================================================

LinearInequality NestedTemplate::buildComponent(
    int idx,
    const std::vector<std::string>& vars) const
{
    LinearInequality result;
    result.strict = false;
    result.motzkin_coef = LinearInequality::ANYTHING;

    const auto& comp_params = component_params_[idx];

    for (size_t i = 0; i < vars.size(); ++i) {
        AffineTerm coef;
        coef.coefficients[comp_params[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }

    AffineTerm const_term;
    const_term.coefficients[comp_params.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;

    return result;
}

// ============================================================================
// φ0: DECREMENT f₀ — loop(x,x') ∧ Σ SI(x) ≥ 0 → f₀(x) - f₀(x') ≥ δ
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhi0_Decrement0(
    const std::vector<LinearInequality>& si_preconditions) const
{
    std::vector<MotzkinContext> contexts;

    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ0: f₀ decrement (poly " + std::to_string(poly_idx) + ")";

        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        std::vector<std::string> loop_in_vars, loop_out_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        }

        // Prémisses SI (injectées de l'extérieur)
        for (const auto& si_precond : si_preconditions) {
            ctx.constraints.push_back(si_precond);
        }

        LinearInequality f0_in = buildComponent(0, loop_in_vars);
        LinearInequality f0_out = buildComponent(0, loop_out_vars);

        // Conclusion inversée : ¬(f₀(x) - f₀(x') ≥ δ)
        LinearInequality neg_decrement;
        neg_decrement.strict = true;
        neg_decrement.motzkin_coef = LinearInequality::ONE;

        for (size_t i = 0; i < loop_in_vars.size(); ++i) {
            AffineTerm coef = f0_in.getCoefficient(loop_in_vars[i]);
            coef.negate();
            neg_decrement.setCoefficient(loop_in_vars[i], coef);
        }
        for (size_t i = 0; i < loop_out_vars.size(); ++i) {
            AffineTerm coef = f0_out.getCoefficient(loop_out_vars[i]);
            neg_decrement.setCoefficient(loop_out_vars[i], coef);
        }

        neg_decrement.constant = neg_decrement.constant - f0_in.constant + f0_out.constant;
        neg_decrement.constant.coefficients[delta_param_] = 1;

        ctx.constraints.push_back(neg_decrement);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// φᵢ: DECREMENT fᵢ — loop(x,x') ∧ Σ SI(x) ≥ 0 → fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) > 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhiI_DecrementI(
    int i,
    const std::vector<LinearInequality>& si_preconditions) const
{
    std::vector<MotzkinContext> contexts;

    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φ" + std::to_string(i) + ": f" + std::to_string(i)
                        + " borrowing (poly " + std::to_string(poly_idx) + ")";

        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        std::vector<std::string> loop_in_vars, loop_out_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        }

        // Prémisses SI (injectées de l'extérieur)
        for (const auto& si_precond : si_preconditions) {
            ctx.constraints.push_back(si_precond);
        }

        LinearInequality fi_in = buildComponent(i, loop_in_vars);
        LinearInequality fi_out = buildComponent(i, loop_out_vars);
        LinearInequality fi_minus_1 = buildComponent(i - 1, loop_in_vars);

        // Conclusion inversée : ¬(fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) > 0)
        LinearInequality neg_decrement;
        neg_decrement.strict = false;
        neg_decrement.motzkin_coef = LinearInequality::ONE;

        for (size_t j = 0; j < loop_in_vars.size(); ++j) {
            AffineTerm coef_in = fi_in.getCoefficient(loop_in_vars[j]);
            coef_in.negate();
            AffineTerm coef_out = fi_out.getCoefficient(loop_out_vars[j]);
            neg_decrement.setCoefficient(loop_in_vars[j], coef_in);
            neg_decrement.setCoefficient(loop_out_vars[j], coef_out);
        }

        for (size_t j = 0; j < loop_in_vars.size(); ++j) {
            AffineTerm coef = fi_minus_1.getCoefficient(loop_in_vars[j]);
            coef.negate();
            AffineTerm current = neg_decrement.getCoefficient(loop_in_vars[j]);
            neg_decrement.setCoefficient(loop_in_vars[j], current + coef);
        }

        neg_decrement.constant = neg_decrement.constant
                               - fi_in.constant + fi_out.constant - fi_minus_1.constant;

        ctx.constraints.push_back(neg_decrement);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// φₙ: BOUNDEDNESS — loop(x,x') ∧ Σ SI(x) ≥ 0 → fₙ₋₁(x) ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
NestedTemplate::generatePhiN_Boundedness(
    const std::vector<LinearInequality>& si_preconditions) const
{
    std::vector<MotzkinContext> contexts;

    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "φₙ: Boundedness (poly " + std::to_string(poly_idx) + ")";

        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        std::vector<std::string> loop_in_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        }

        // Prémisses SI (injectées de l'extérieur)
        for (const auto& si_precond : si_preconditions) {
            ctx.constraints.push_back(si_precond);
        }

        // Conclusion inversée : ¬(fₙ₋₁(x) ≥ 0)
        LinearInequality fn_minus_1 = buildComponent(num_components_ - 1, loop_in_vars);

        LinearInequality neg_boundedness;
        neg_boundedness.strict = true;
        neg_boundedness.motzkin_coef = LinearInequality::ONE;

        for (size_t i = 0; i < loop_in_vars.size(); ++i) {
            AffineTerm coef = fn_minus_1.getCoefficient(loop_in_vars[i]);
            coef.negate();
            neg_boundedness.setCoefficient(loop_in_vars[i], coef);
        }

        neg_boundedness.constant = fn_minus_1.constant;
        neg_boundedness.constant.negate();

        ctx.constraints.push_back(neg_boundedness);
        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}
