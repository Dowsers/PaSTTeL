#include <cmath>
#include <iostream>
#include <sstream>

#include "templates/multiphase_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

MultiphaseTemplate::MultiphaseTemplate(int num_phases, int delta_value)
    : num_phases_(num_phases)
    , delta_value_(delta_value)
    , initialized_(false)
{
    if (num_phases_ < 2) {
        throw std::invalid_argument("MultiphaseTemplate requires at least 2 phases");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  MULTIPHASE TEMPLATE                       ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Phases: " << num_phases_ << std::endl;
    }
}

// ============================================================================
// INITIALISATION
// ============================================================================

void MultiphaseTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    int n = static_cast<int>(lasso_.program_vars.size());

    generators_.clear();
    delta_params_.clear();
    for (int i = 0; i < num_phases_; ++i) {
        generators_.push_back(
            std::make_unique<AffineFunctionGenerator>("MP_RANK_" + std::to_string(i), n));
        delta_params_.push_back("MP_DELTA_" + std::to_string(i));
    }
    initialized_ = true;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  Variables:  " << n << std::endl;
        std::cout << "  RF params:  " << getParameters().getTotalParameterCount() << std::endl;
    }
}

// ============================================================================
// DECLARATION DES PARAMETRES SMT
// ============================================================================

void MultiphaseTemplate::declareParameters(SMTSolverInterface* solver) const {
    if (!initialized_) {
        throw std::runtime_error("MultiphaseTemplate::declareParameters() called before init()");
    }

    auto phantom_mask = lasso_.loopPhantomVarMask();
    for (const auto& gen : generators_) {
        gen->declareParameters(solver, phantom_mask);
    }

    // Every delta must be strictly positive -- not just delta_params_[0].
    // An unconstrained (or non-positive) delta_i would let the solver
    // trivially satisfy phi_decr_i's "own decrease" disjunct, or make the
    // escape f_{i-1}(x)>0 irrelevant, without a real decrease.
    for (const auto& delta : delta_params_) {
        solver->declareVariable(delta, "Real");
        solver->addAssertion("(> " + delta + " " + std::to_string(delta_value_) + ")");
    }
}

// ============================================================================
// RETOURNE LES PARAMETRES (pour extractParameters dans le synthesizer)
// ============================================================================

RankingTemplate::TemplateParameters MultiphaseTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("MultiphaseTemplate::getParameters() called before init()");
    }

    TemplateParameters params;
    for (const auto& gen : generators_) {
        const auto& names = gen->getParamNames();
        params.ranking_params.insert(params.ranking_params.end(), names.begin(), names.end());
    }

    // First delta as the "main" delta; the rest ride along as ranking params.
    params.delta_param = delta_params_[0];
    params.delta_value = delta_value_;
    for (size_t i = 1; i < delta_params_.size(); ++i) {
        params.ranking_params.push_back(delta_params_[i]);
    }

    return params;
}

// ============================================================================
// phi_decr_0 (unconditional) et phi_decr_i, i>=1 (decrease OR escape to phase i-1)
// ============================================================================

std::vector<RankingTemplate::ConclusionPart> MultiphaseTemplate::getConstraintsDec(
    const std::vector<std::string>& in_vars,
    const std::vector<std::string>& out_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("MultiphaseTemplate::getConstraintsDec() called before init()");
    }

    std::vector<ConclusionPart> parts;

    // phi_decr_0 : f_0(x) - f_0(x') > delta_0   (no escape -- always active first)
    {
        LinearInequality li = generators_[0]->generate(in_vars);
        LinearInequality li2 = generators_[0]->generate(out_vars);
        li2.negate();
        li = li + li2;
        li.constant.coefficients[delta_params_[0]] -= 1.0;
        li.strict = true;
        li.motzkin_coef = LinearInequality::ONE;
        parts.push_back({li});
    }

    // phi_decr_i, i = 1 .. k-1 : f_i(x)-f_i(x') > delta_i  OR  f_{i-1}(x) > 0
    for (int i = 1; i < num_phases_; ++i) {
        ConclusionPart part;

        LinearInequality own = generators_[i]->generate(in_vars);
        LinearInequality own_out = generators_[i]->generate(out_vars);
        own_out.negate();
        own = own + own_out;
        own.constant.coefficients[delta_params_[i]] -= 1.0;
        own.strict = true;
        own.motzkin_coef = LinearInequality::ANYTHING;
        part.push_back(own);

        LinearInequality escape = generators_[i - 1]->generate(in_vars);
        escape.strict = true;
        escape.motzkin_coef = LinearInequality::ANYTHING;
        part.push_back(escape);

        parts.push_back(part);
    }

    return parts;
}

// ============================================================================
// phi_bound : f_{k-1}(x) >= 0 -- last phase only
// ============================================================================

std::vector<RankingTemplate::ConclusionPart> MultiphaseTemplate::getConstraintsBounded(
    const std::vector<std::string>& in_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("MultiphaseTemplate::getConstraintsBounded() called before init()");
    }

    LinearInequality li = generators_[num_phases_ - 1]->generate(in_vars);
    li.strict = false;
    li.motzkin_coef = LinearInequality::ONE;
    return { {li} };
}

// ============================================================================
// EXTRACTION DES RESULTATS
// ============================================================================

std::vector<RankingFunction> MultiphaseTemplate::extractRankingFunctions(
    SMTSolverInterface* solver,
    const std::vector<std::string>& program_vars) const
{
    std::vector<RankingFunction> components;
    size_t n = program_vars.size();

    for (int i = 0; i < num_phases_; ++i) {
        RankingFunction rf;
        auto values = generators_[i]->extractRationals(solver);
        for (size_t j = 0; j < n && j < values.size(); ++j)
            rf.coefficients[program_vars[j]] = values[j];

        if (values.size() > n)
            rf.constant = values[n];

        rf.delta = solver->getRationalValue2(delta_params_[i]);
        components.push_back(rf);
    }
    return components;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

std::string MultiphaseTemplate::getDescription() const {
    std::ostringstream oss;
    oss << num_phases_ << "-multiphase: f0 decreases unconditionally, "
        << "each fi (i>0) decreases once phase i-1 is exhausted";
    return oss.str();
}

void MultiphaseTemplate::printInfo() const {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n┌─ Template Info ──────────┐" << std::endl;
        std::cout << "│ Name: " << getName() << std::endl;
        std::cout << "│ Phases: " << num_phases_ << std::endl;
        if (initialized_) {
            std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
            std::cout << "│ RF params: " << getParameters().getTotalParameterCount() << std::endl;
        }
        std::cout << "└──────────────────────────┘" << std::endl;
    }
}
