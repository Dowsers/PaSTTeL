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
// DECLARATION DES PARAMETRES SMT
// ============================================================================

void LexicographicTemplate::declareParameters(SMTSolverInterface* solver) const {
    if (!initialized_) {
        throw std::runtime_error("LexicographicTemplate::declareParameters() called before init()");
    }

    for (const auto& comp_params : component_params_) {
        for (const auto& param : comp_params) {
            solver->declareVariable(param, "Real");
        }
    }

    // Every delta must be strictly positive -- not just delta_params_[0].
    // phi_consec/phi_decrement use dj/di as free slack; an unconstrained
    // (or non-positive) delta would let the solver trivially satisfy the
    // disjunction without a real decrease, breaking soundness.
    for (const auto& delta : delta_params_) {
        solver->declareVariable(delta, "Real");
        solver->addAssertion("(> " + delta + " " + std::to_string(delta_value_) + ")");
    }
}

// ============================================================================
// IMPLEMENTATION DE L'INTERFACE
// ============================================================================

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

LinearInequality LexicographicTemplate::buildComponentDiff(
    int idx,
    const std::vector<std::string>& in_vars,
    const std::vector<std::string>& out_vars) const
{
    // fi(x) - fi(x')
    LinearInequality li = buildComponent(idx, in_vars);
    LinearInequality li2 = buildComponent(idx, out_vars);
    li2.negate();
    return li + li2;
}

// ============================================================================
// phi_bound: BOUNDEDNESS -- loop(x,x') -> fi(x) > 0, for each component i
// ============================================================================

std::vector<RankingTemplate::ConclusionPart> LexicographicTemplate::getConstraintsBounded(
    const std::vector<std::string>& in_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("LexicographicTemplate::getConstraintsBounded() called before init()");
    }

    std::vector<ConclusionPart> parts;

    for (int i = 0; i < num_components_; ++i) {
        LinearInequality atom = buildComponent(i, in_vars);
        atom.strict = true;
        atom.motzkin_coef = LinearInequality::ONE;
        parts.push_back({atom});
    }

    return parts;
}

// ============================================================================
// phi_consec_i (i < k-1) and phi_decrement -- disjunctive decrease conclusions
//
// phi_consec_i : fi(x') <= fi(x)  OR  exists j<i : fj(x) - fj(x') > dj
// phi_decrement:                       exists i   : fi(x) - fi(x') > di
// ============================================================================

std::vector<RankingTemplate::ConclusionPart> LexicographicTemplate::getConstraintsDec(
    const std::vector<std::string>& in_vars,
    const std::vector<std::string>& out_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("LexicographicTemplate::getConstraintsDec() called before init()");
    }

    std::vector<ConclusionPart> parts;

    // phi_consec_i, i = 0 .. k-2
    for (int i = 0; i < num_components_ - 1; ++i) {
        ConclusionPart part;

        // atom: fi(x) - fi(x') >= 0   (i.e. fi(x') <= fi(x))
        LinearInequality non_incr = buildComponentDiff(i, in_vars, out_vars);
        non_incr.strict = false;
        non_incr.motzkin_coef = LinearInequality::ONE;
        part.push_back(non_incr);

        // atom: fj(x) - fj(x') > dj, for each j < i
        for (int j = 0; j < i; ++j) {
            LinearInequality decr_j = buildComponentDiff(j, in_vars, out_vars);
            decr_j.constant.coefficients[delta_params_[j]] -= 1.0;
            decr_j.strict = true;
            decr_j.motzkin_coef = LinearInequality::ANYTHING;
            part.push_back(decr_j);
        }

        parts.push_back(part);
    }

    // phi_decrement : exists i : fi(x) - fi(x') > di, for all i = 0 .. k-1
    {
        ConclusionPart part;
        for (int i = 0; i < num_components_; ++i) {
            LinearInequality decr_i = buildComponentDiff(i, in_vars, out_vars);
            decr_i.constant.coefficients[delta_params_[i]] -= 1.0;
            decr_i.strict = true;
            decr_i.motzkin_coef = LinearInequality::ANYTHING;
            part.push_back(decr_i);
        }
        parts.push_back(part);
    }

    return parts;
}

// ============================================================================
// EXTRACTION DES RÉSULTATS
// ============================================================================

std::vector<RankingFunction> LexicographicTemplate::extractRankingFunctions(
    SMTSolverInterface* solver,
    const std::vector<std::string>& program_vars) const
{
    std::vector<RankingFunction> components;
    size_t n = program_vars.size();

    for (int i = 0; i < num_components_; ++i) {
        RankingFunction rf;
        for (size_t j = 0; j < n && j < component_params_[i].size() - 1; ++j) {
            rf.coefficients[program_vars[j]] = solver->getRationalValue2(component_params_[i][j]);
        }
        if (!component_params_[i].empty()) {
            rf.constant = solver->getRationalValue2(component_params_[i].back());
        }
        if (i < static_cast<int>(delta_params_.size())) {
            rf.delta = solver->getRationalValue2(delta_params_[i]);
        }
        components.push_back(rf);
    }
    return components;
}
