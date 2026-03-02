#include <cmath>
#include <iostream>
#include <sstream>

#include "templates/lexicographic_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

LexicographicTemplate::LexicographicTemplate(int num_components, int delta_value)
    : num_components_(num_components)
    , delta_value_(delta_value)
    , initialized_(false)
{
    if (num_components_ < 2) {
        throw std::invalid_argument("LexicographicTemplate requires at least 2 components");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  LEXICOGRAPHIC TEMPLATE                    ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Components: " << num_components_ << std::endl;
    }
}

// ============================================================================
// INITIALISATION
// ============================================================================

void LexicographicTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    initializeParameters();
    initialized_ = true;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  Variables:  " << lasso_.program_vars.size() << std::endl;
        std::cout << "  RF params:  " << getParameters().getTotalParameterCount() << std::endl;
    }
}

void LexicographicTemplate::initializeParameters() {
    int n = lasso_.program_vars.size();

    // Parameters for each component fi
    component_params_.clear();
    for (int i = 0; i < num_components_; ++i) {
        std::vector<std::string> comp_params;
        for (int j = 0; j < n; ++j) {
            comp_params.push_back("LEX_RANK_" + std::to_string(i) + "_" + std::to_string(j));
        }
        comp_params.push_back("LEX_RANK_" + std::to_string(i) + "_const");
        component_params_.push_back(comp_params);
    }

    // One delta per component
    delta_params_.clear();
    for (int i = 0; i < num_components_; ++i) {
        delta_params_.push_back("LEX_DELTA_" + std::to_string(i));
    }
}

// ============================================================================
// IMPLEMENTATION DE L'INTERFACE
// ============================================================================

std::vector<RankingTemplate::MotzkinContext> LexicographicTemplate::getConstraints(
    const std::vector<LinearInequality>& /*si_preconditions*/) const
{
    if (!initialized_) {
        throw std::runtime_error("LexicographicTemplate::getConstraints() called before init()");
    }

    std::vector<MotzkinContext> all_contexts;
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n┌─ Generating Lexicographic Constraints ─┐" << std::endl;

    // phi_bound: fi(x) > 0 for each component (k contexts per polyhedron)
    auto bound = generateBoundedness();
    if (verbose)
        std::cout << "│ phi_bound: Boundedness : " << bound.size() << std::endl;
    all_contexts.insert(all_contexts.end(), bound.begin(), bound.end());

    // phi_consec: lexicographic consecution (k-1 contexts per polyhedron)
    auto consec = generateConsecution();
    if (verbose)
        std::cout << "│ phi_consec: Consecution : " << consec.size() << std::endl;
    all_contexts.insert(all_contexts.end(), consec.begin(), consec.end());

    // phi_decrement: at least one component decreases (1 context per polyhedron)
    auto decr = generateDecrement();
    if (verbose)
        std::cout << "│ phi_decrement: Decrement : " << decr.size() << std::endl;
    all_contexts.insert(all_contexts.end(), decr.begin(), decr.end());

    if (verbose) {
        std::cout << "└─────────────────────────────────────────┘" << std::endl;
        std::cout << "  Total contexts: " << all_contexts.size() << std::endl;
    }
    return all_contexts;
}

RankingTemplate::TemplateParameters LexicographicTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("getParameters() called before init()");
    }

    TemplateParameters params;

    // All component parameters
    for (const auto& comp_params : component_params_) {
        for (const auto& param : comp_params) {
            params.ranking_params.push_back(param);
        }
    }

    // First delta as the "main" delta
    params.delta_param = delta_params_[0];
    params.delta_value = delta_value_;

    // Remaining delta params treated as synthesized ranking params
    for (size_t i = 1; i < delta_params_.size(); ++i) {
        params.ranking_params.push_back(delta_params_[i]);
    }

    return params;
}

std::string LexicographicTemplate::getDescription() const {
    std::ostringstream oss;
    oss << num_components_ << "-lex: lexicographic decrease of " << num_components_ << " components";
    return oss.str();
}

void LexicographicTemplate::printInfo() const {
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
// CONSTRUCTION DES TERMES
// ============================================================================

LinearInequality LexicographicTemplate::buildComponent(
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
// phi_bound: BOUNDEDNESS — loop(x,x') -> fi(x) > 0 for each i
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
LexicographicTemplate::generateBoundedness() const {
    std::vector<MotzkinContext> contexts;

    for (int i = 0; i < num_components_; ++i) {
        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            MotzkinContext ctx;
            ctx.annotation = "phi_bound: f" + std::to_string(i) +
                            " > 0 (poly " + std::to_string(poly_idx) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            std::vector<std::string> loop_in_vars;
            for (const auto& var : lasso_.program_vars) {
                loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            }

            // fi(x) > 0 — Negation: -fi(x) >= 0
            LinearInequality fi = buildComponent(i, loop_in_vars);

            LinearInequality neg_bound;
            neg_bound.strict = false;
            neg_bound.motzkin_coef = LinearInequality::ONE;

            for (size_t j = 0; j < loop_in_vars.size(); ++j) {
                AffineTerm coef = fi.getCoefficient(loop_in_vars[j]);
                coef.negate();
                neg_bound.setCoefficient(loop_in_vars[j], coef);
            }

            neg_bound.constant = fi.constant;
            neg_bound.constant.negate();

            ctx.constraints.push_back(neg_bound);
            contexts.push_back(ctx);
            poly_idx++;
        }
    }

    return contexts;
}

// ============================================================================
// phi_consec: CONSECUTION — for each i < k-1:
//   loop(x,x') -> fi(x') <= fi(x)  OR  exists j<i : fj(x) - fj(x') > dj
//
// Encoding as Motzkin negation:
//   NOT( fi(x') <= fi(x)  OR  exists j<i : ... )
//   = fi(x') > fi(x)  AND  forall j<i : fj(x) - fj(x') <= dj
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
LexicographicTemplate::generateConsecution() const {
    std::vector<MotzkinContext> contexts;

    for (int i = 0; i < num_components_ - 1; ++i) {
        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            MotzkinContext ctx;
            ctx.annotation = "phi_consec: f" + std::to_string(i) +
                            " consecution (poly " + std::to_string(poly_idx) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            std::vector<std::string> loop_in_vars, loop_out_vars;
            for (const auto& var : lasso_.program_vars) {
                loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
                loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
            }

            // Negation part 1: fi(x') - fi(x) > 0
            LinearInequality fi_in = buildComponent(i, loop_in_vars);
            LinearInequality fi_out = buildComponent(i, loop_out_vars);

            LinearInequality neg_nonincr;
            neg_nonincr.strict = true;
            neg_nonincr.motzkin_coef = LinearInequality::ONE;

            for (size_t j = 0; j < loop_out_vars.size(); ++j) {
                AffineTerm coef = fi_out.getCoefficient(loop_out_vars[j]);
                neg_nonincr.setCoefficient(loop_out_vars[j], coef);
            }
            for (size_t j = 0; j < loop_in_vars.size(); ++j) {
                AffineTerm coef = fi_in.getCoefficient(loop_in_vars[j]);
                coef.negate();
                neg_nonincr.setCoefficient(loop_in_vars[j], coef);
            }
            neg_nonincr.constant = fi_out.constant - fi_in.constant;

            ctx.constraints.push_back(neg_nonincr);

            // Negation part 2: for each j < i, dj - fj(x) + fj(x') >= 0
            for (int j = 0; j < i; ++j) {
                LinearInequality fj_in = buildComponent(j, loop_in_vars);
                LinearInequality fj_out = buildComponent(j, loop_out_vars);

                LinearInequality neg_decr_j;
                neg_decr_j.strict = false;
                neg_decr_j.motzkin_coef = LinearInequality::ANYTHING;

                for (size_t v = 0; v < loop_in_vars.size(); ++v) {
                    AffineTerm coef = fj_in.getCoefficient(loop_in_vars[v]);
                    coef.negate();
                    neg_decr_j.setCoefficient(loop_in_vars[v], coef);
                }
                for (size_t v = 0; v < loop_out_vars.size(); ++v) {
                    AffineTerm coef = fj_out.getCoefficient(loop_out_vars[v]);
                    neg_decr_j.setCoefficient(loop_out_vars[v], coef);
                }
                neg_decr_j.constant = fj_out.constant - fj_in.constant;
                neg_decr_j.constant.coefficients[delta_params_[j]] = 1.0;

                ctx.constraints.push_back(neg_decr_j);
            }

            contexts.push_back(ctx);
            poly_idx++;
        }
    }

    return contexts;
}

// ============================================================================
// phi_decrement: DECREMENT — loop(x,x') -> exists i : fi(x) - fi(x') > di
//
// Negation: forall i : di - fi(x) + fi(x') >= 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
LexicographicTemplate::generateDecrement() const {
    std::vector<MotzkinContext> contexts;

    int poly_idx = 0;
    for (const auto& polyhedron : lasso_.loop.polyhedra) {
        MotzkinContext ctx;
        ctx.annotation = "phi_decrement: at least one component decreases (poly "
                         + std::to_string(poly_idx) + ")";

        for (const auto& ineq : polyhedron) {
            ctx.constraints.push_back(ineq);
        }

        std::vector<std::string> loop_in_vars, loop_out_vars;
        for (const auto& var : lasso_.program_vars) {
            loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
            loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        }

        for (int i = 0; i < num_components_; ++i) {
            LinearInequality fi_in = buildComponent(i, loop_in_vars);
            LinearInequality fi_out = buildComponent(i, loop_out_vars);

            LinearInequality neg_decr;
            neg_decr.strict = false;
            neg_decr.motzkin_coef = LinearInequality::ANYTHING;

            for (size_t j = 0; j < loop_in_vars.size(); ++j) {
                AffineTerm coef = fi_in.getCoefficient(loop_in_vars[j]);
                coef.negate();
                neg_decr.setCoefficient(loop_in_vars[j], coef);
            }
            for (size_t j = 0; j < loop_out_vars.size(); ++j) {
                AffineTerm coef = fi_out.getCoefficient(loop_out_vars[j]);
                neg_decr.setCoefficient(loop_out_vars[j], coef);
            }
            neg_decr.constant = fi_out.constant - fi_in.constant;
            neg_decr.constant.coefficients[delta_params_[i]] = 1.0;

            ctx.constraints.push_back(neg_decr);
        }

        contexts.push_back(ctx);
        poly_idx++;
    }

    return contexts;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::vector<RankingFunction> LexicographicTemplate::extractRankingFunctions(
    std::shared_ptr<SMTSolver> solver,
    const std::vector<std::string>& program_vars) const
{
    std::vector<RankingFunction> components;
    size_t n = program_vars.size();

    for (int i = 0; i < num_components_; ++i) {
        RankingFunction rf;
        for (size_t j = 0; j < n && j < component_params_[i].size() - 1; ++j) {
            rf.coefficients[program_vars[j]] = static_cast<int64_t>(std::round(solver->getValue(component_params_[i][j])));
        }
        if (!component_params_[i].empty()) {
            rf.constant = static_cast<int64_t>(std::round(solver->getValue(component_params_[i].back())));
        }
        if (i < static_cast<int>(delta_params_.size())) {
            rf.delta = static_cast<int64_t>(std::round(solver->getValue(delta_params_[i])));
        }
        components.push_back(rf);
    }
    return components;
}
