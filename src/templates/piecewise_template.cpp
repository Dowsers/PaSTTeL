#include <cmath>
#include <iostream>
#include <sstream>

#include "templates/piecewise_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

PiecewiseTemplate::PiecewiseTemplate(int num_pieces, int delta_value)
    : num_pieces_(num_pieces)
    , delta_value_(delta_value)
    , initialized_(false)
{
    if (num_pieces_ < 2) {
        throw std::invalid_argument("PiecewiseTemplate requires at least 2 pieces");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  PIECEWISE TEMPLATE                        ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Pieces: " << num_pieces_ << std::endl;
    }
}

// ============================================================================
// INITIALISATION
// ============================================================================

void PiecewiseTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    int n = static_cast<int>(lasso_.program_vars.size());

    rank_generators_.clear();
    guard_generators_.clear();
    delta_params_.clear();
    for (int i = 0; i < num_pieces_; ++i) {
        rank_generators_.push_back(
            std::make_unique<AffineFunctionGenerator>("PW_RANK_" + std::to_string(i), n));
        guard_generators_.push_back(
            std::make_unique<AffineFunctionGenerator>("PW_GUARD_" + std::to_string(i), n));
        delta_params_.push_back("PW_DELTA_" + std::to_string(i));
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

void PiecewiseTemplate::declareParameters(SMTSolverInterface* solver) const {
    if (!initialized_) {
        throw std::runtime_error("PiecewiseTemplate::declareParameters() called before init()");
    }

    for (const auto& gen : rank_generators_) gen->declareParameters(solver);
    for (const auto& gen : guard_generators_) gen->declareParameters(solver);

    // Every delta must be strictly positive -- not just delta_params_[0].
    // An unconstrained delta_i would let the solver satisfy phi_decr_i's
    // "own decrease" disjunct without a real decrease.
    for (const auto& delta : delta_params_) {
        solver->declareVariable(delta, "Real");
        solver->addAssertion("(> " + delta + " " + std::to_string(delta_value_) + ")");
    }
}

// ============================================================================
// RETOURNE LES PARAMETRES (pour extractParameters dans le synthesizer)
// ============================================================================

RankingTemplate::TemplateParameters PiecewiseTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("PiecewiseTemplate::getParameters() called before init()");
    }

    TemplateParameters params;
    for (const auto& gen : rank_generators_) {
        const auto& names = gen->getParamNames();
        params.ranking_params.insert(params.ranking_params.end(), names.begin(), names.end());
    }
    for (const auto& gen : guard_generators_) {
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
// phi_decr_i : -h_i(x)>0  OR  f_i(x)-f_i(x')>=delta_i
// ============================================================================

std::vector<RankingTemplate::ConclusionPart> PiecewiseTemplate::getConstraintsDec(
    const std::vector<std::string>& in_vars,
    const std::vector<std::string>& out_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("PiecewiseTemplate::getConstraintsDec() called before init()");
    }

    std::vector<ConclusionPart> parts;

    for (int i = 0; i < num_pieces_; ++i) {
        ConclusionPart part;

        // -h_i(x) > 0  (piece i's guard does not hold)
        LinearInequality not_guard = guard_generators_[i]->generate(in_vars);
        not_guard.negate();
        not_guard.strict = true;
        not_guard.motzkin_coef = LinearInequality::ANYTHING;
        part.push_back(not_guard);

        // f_i(x) - f_i(x') >= delta_i
        LinearInequality own = rank_generators_[i]->generate(in_vars);
        LinearInequality own_out = rank_generators_[i]->generate(out_vars);
        own_out.negate();
        own = own + own_out;
        own.constant.coefficients[delta_params_[i]] -= 1.0;
        own.strict = false;
        own.motzkin_coef = LinearInequality::ANYTHING;
        part.push_back(own);

        parts.push_back(part);
    }

    return parts;
}

// ============================================================================
// phi_bound_i : -h_i(x)>0 OR f_i(x)>=0     phi_exhaustive : OR_i h_i(x)>=0
// ============================================================================

std::vector<RankingTemplate::ConclusionPart> PiecewiseTemplate::getConstraintsBounded(
    const std::vector<std::string>& in_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("PiecewiseTemplate::getConstraintsBounded() called before init()");
    }

    std::vector<ConclusionPart> parts;

    for (int i = 0; i < num_pieces_; ++i) {
        ConclusionPart part;

        LinearInequality not_guard = guard_generators_[i]->generate(in_vars);
        not_guard.negate();
        not_guard.strict = true;
        not_guard.motzkin_coef = LinearInequality::ANYTHING;
        part.push_back(not_guard);

        LinearInequality bound = rank_generators_[i]->generate(in_vars);
        bound.strict = false;
        bound.motzkin_coef = LinearInequality::ANYTHING;
        part.push_back(bound);

        parts.push_back(part);
    }

    // Exhaustiveness: every transition falls into at least one piece.
    {
        ConclusionPart exhaustive;
        for (int i = 0; i < num_pieces_; ++i) {
            LinearInequality guard = guard_generators_[i]->generate(in_vars);
            guard.strict = false;
            guard.motzkin_coef = LinearInequality::ANYTHING;
            exhaustive.push_back(guard);
        }
        parts.push_back(exhaustive);
    }

    return parts;
}

// ============================================================================
// EXTRACTION DES RESULTATS
// ============================================================================

std::vector<RankingFunction> PiecewiseTemplate::extractRankingFunctions(
    SMTSolverInterface* solver,
    const std::vector<std::string>& program_vars) const
{
    std::vector<RankingFunction> components;
    size_t n = program_vars.size();

    for (int i = 0; i < num_pieces_; ++i) {
        RankingFunction rf;
        auto values = rank_generators_[i]->extractRationals(solver);
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

std::string PiecewiseTemplate::getDescription() const {
    std::ostringstream oss;
    oss << num_pieces_ << "-piecewise: each fi decreases within its own guarded piece";
    return oss.str();
}

void PiecewiseTemplate::printInfo() const {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n┌─ Template Info ──────────┐" << std::endl;
        std::cout << "│ Name: " << getName() << std::endl;
        std::cout << "│ Pieces: " << num_pieces_ << std::endl;
        if (initialized_) {
            std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
            std::cout << "│ RF params: " << getParameters().getTotalParameterCount() << std::endl;
        }
        std::cout << "└──────────────────────────┘" << std::endl;
    }
}
