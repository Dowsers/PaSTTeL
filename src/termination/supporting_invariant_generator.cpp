#include "termination/supporting_invariant_generator.h"

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

SupportingInvariantGenerator::SupportingInvariantGenerator(
    int num_si_strict, int num_si_nonstrict)
    : num_strict_(num_si_strict)
    , num_nonstrict_(num_si_nonstrict)
    , num_si_(num_si_strict + num_si_nonstrict)
    , initialized_(false)
{}

// ============================================================================
// INITIALISATION
// ============================================================================

void SupportingInvariantGenerator::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    initializeParameters();
    initialized_ = true;
}

void SupportingInvariantGenerator::initializeParameters() {
    int n = static_cast<int>(lasso_.program_vars.size());
    si_params_.clear();

    for (int si_idx = 0; si_idx < num_si_; ++si_idx) {
        std::vector<std::string> si_coeffs;
        for (int i = 0; i < n; ++i) {
            si_coeffs.push_back(
                "SUP_INVAR_" + std::to_string(si_idx) + "_" + std::to_string(i));
        }
        si_coeffs.push_back("SUP_INVAR_" + std::to_string(si_idx) + "_const");
        si_params_.push_back(si_coeffs);
    }
}

// ============================================================================
// DÉCLARATION DES PARAMÈTRES SMT
// ============================================================================

void SupportingInvariantGenerator::declareParameters(
    std::shared_ptr<SMTSolver> solver) const
{
    for (const auto& si_param_set : si_params_) {
        for (const auto& param : si_param_set) {
            solver->declareVariable(param, "Real");
        }
    }
}

// ============================================================================
// CONSTRUCTION D'UN TERME SI
// ============================================================================

LinearInequality SupportingInvariantGenerator::buildSI(
    int si_idx,
    const std::vector<std::string>& vars,
    bool is_strict) const
{
    LinearInequality result;
    result.strict = is_strict;
    result.motzkin_coef = LinearInequality::ANYTHING;

    const auto& si_coeffs = si_params_[si_idx];

    for (size_t i = 0; i < vars.size(); ++i) {
        AffineTerm coef;
        coef.coefficients[si_coeffs[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }

    AffineTerm const_term;
    const_term.coefficients[si_coeffs.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;

    return result;
}

// ============================================================================
// PRÉMISSES POUR LES TEMPLATES RF
// ============================================================================

std::vector<LinearInequality> SupportingInvariantGenerator::buildPreconditions(
    const std::vector<std::string>& vars) const
{
    std::vector<LinearInequality> preconditions;
    for (int si_idx = 0; si_idx < num_si_; ++si_idx) {
        bool is_strict = (si_idx < num_strict_);
        preconditions.push_back(buildSI(si_idx, vars, is_strict));
    }
    return preconditions;
}

// ============================================================================
// φ1 : STEM INITIATION — stem(x,x') → SI(x') ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
SupportingInvariantGenerator::generatePhi1() const
{
    std::vector<RankingTemplate::MotzkinContext> contexts;

    for (int si_idx = 0; si_idx < num_si_; ++si_idx) {
        bool is_strict = (si_idx < num_strict_);

        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.stem.polyhedra) {
            RankingTemplate::MotzkinContext ctx;
            ctx.annotation = "φ1: SI_" + std::to_string(si_idx)
                           + " initiation (poly " + std::to_string(poly_idx) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            std::vector<std::string> stem_out_vars;
            for (const auto& var : lasso_.program_vars) {
                stem_out_vars.push_back(lasso_.stem.getSSAVar(var, true));
            }

            // ¬(SI(x') ≥ 0) = -SI(x') > 0
            LinearInequality neg_si = buildSI(si_idx, stem_out_vars, is_strict);
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
// φ2 : LOOP CONSECUTION — SI(x) ∧ loop(x,x') → SI(x') ≥ 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
SupportingInvariantGenerator::generatePhi2() const
{
    std::vector<RankingTemplate::MotzkinContext> contexts;

    for (int si_idx = 0; si_idx < num_si_; ++si_idx) {
        bool is_strict = (si_idx < num_strict_);

        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            RankingTemplate::MotzkinContext ctx;
            ctx.annotation = "φ2: SI_" + std::to_string(si_idx)
                           + " consecution (poly " + std::to_string(poly_idx) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            std::vector<std::string> loop_in_vars, loop_out_vars;
            for (const auto& var : lasso_.program_vars) {
                loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
                loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
            }

            // Prémisse : SI(x) ≥ 0
            LinearInequality si_precond = buildSI(si_idx, loop_in_vars, is_strict);
            ctx.constraints.push_back(si_precond);

            // Conclusion inversée : ¬(SI(x') ≥ 0)
            LinearInequality neg_si_prime = buildSI(si_idx, loop_out_vars, is_strict);
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
// ACCESSEURS POUR L'EXTRACTION
// ============================================================================

std::vector<std::string> SupportingInvariantGenerator::getSIParams() const {
    std::vector<std::string> flat;
    for (const auto& si_param_set : si_params_) {
        for (const auto& param : si_param_set) {
            flat.push_back(param);
        }
    }
    return flat;
}

std::vector<bool> SupportingInvariantGenerator::getSIIsStrict() const {
    std::vector<bool> result;
    for (int si_idx = 0; si_idx < num_si_; ++si_idx) {
        result.push_back(si_idx < num_strict_);
    }
    return result;
}
