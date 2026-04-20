#include <cmath>
#include <iostream>

#include "templates/affine_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

AffineTemplate::AffineTemplate(int delta_value)
    : delta_value_(delta_value)
    , delta_param_("DELTA")
    , initialized_(false)
{
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║            AFFINE TEMPLATE                 ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
    }
}

// ============================================================================
// INITIALISATION
// ============================================================================

void AffineTemplate::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    // template vars = loop.getOutVars() ∩ loop.getInVars()
    const auto& vars = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
    int n = static_cast<int>(vars.size());
    generator_ = std::make_unique<AffineFunctionGenerator>("RANKING_C", n);
    initialized_ = true;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  Variables:  " << n << std::endl;
        std::cout << "  RF params:  " << generator_->getParamNames().size() << std::endl;
    }
}

// ============================================================================
// DECLARATION DES PARAMETRES SMT
// ============================================================================

void AffineTemplate::declareParameters(std::shared_ptr<SMTSolver> solver) const {
    if (!initialized_) {
        throw std::runtime_error("AffineTemplate::declareParameters() called before init()");
    }
    generator_->declareParameters(solver);
    solver->declareVariable(delta_param_, "Real");
    solver->addAssertion("(> " + delta_param_ + " " + std::to_string(delta_value_) + ")");
}

// ============================================================================
// RETOURNE LES PARAMETRES (pour extractParameters dans le synthesizer)
// ============================================================================

RankingTemplate::TemplateParameters AffineTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("AffineTemplate::getParameters() called before init()");
    }
    TemplateParameters params;
    params.ranking_params = generator_->getParamNames();
    params.delta_param = delta_param_;
    params.delta_value = delta_value_;
    return params;
}

// ============================================================================
// CONCLUSION POSITIVE DE DECROISSANCE : f(x) - f(x')  >= delta
// ============================================================================

std::vector<LinearInequality> AffineTemplate::getConstraintsDec(
    const std::vector<std::string>& in_vars,
    const std::vector<std::string>& out_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("AffineTemplate::getConstraintsDec() called before init()");
    }

    // f(x)
    LinearInequality li = generator_->generate(in_vars);
    // f(x') puis negate -> -f(x')
    LinearInequality li2 = generator_->generate(out_vars);
    li2.negate();
    // f(x) - f(x')
    li = li + li2;
    // f(x) - f(x') - delta  (soustrait delta de la constante)
    li.constant.coefficients[delta_param_] -= 1.0;
    // conclusion stricte : f(x) - f(x') - delta >= 0
    li.strict = false;
    li.motzkin_coef = LinearInequality::ONE;

    return {li};
}

// ============================================================================
// CONCLUSION POSITIVE DE BORNAGE : f(x) >= 0
// ============================================================================

LinearInequality AffineTemplate::getConstraintsBounded(
    const std::vector<std::string>& in_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("AffineTemplate::getConstraintsBounded() called before init()");
    }

    LinearInequality li = generator_->generate(in_vars);
    li.strict = false;
    li.motzkin_coef = LinearInequality::ONE;
    return li;
}

// ============================================================================
// EXTRACTION DES RESULTATS
// ============================================================================

std::vector<RankingFunction> AffineTemplate::extractRankingFunctions(
    std::shared_ptr<SMTSolver> solver,
    const std::vector<std::string>& program_vars) const
{
    RankingFunction rf;
    auto values = generator_->extractValues(solver);
    size_t n = program_vars.size();

    for (size_t i = 0; i < n && i < values.size(); ++i) {
        rf.coefficients[program_vars[i]] = static_cast<int64_t>(std::round(values[i]));
    }
    if (values.size() > n) {
        rf.constant = static_cast<int64_t>(std::round(values[n]));  // prefix_const
    }
    rf.delta = static_cast<int64_t>(std::round(solver->getValue(delta_param_)));

    return {rf};
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void AffineTemplate::printInfo() const {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n┌─ Template Info ──────────┐" << std::endl;
        std::cout << "│ Name: " << getName() << std::endl;
        std::cout << "│ Description: " << getDescription() << std::endl;
        if (initialized_) {
            std::cout << "│ Variables: " << lasso_.program_vars.size() << std::endl;
            std::cout << "│ RF params: " << generator_->getParamNames().size() << std::endl;
        }
        std::cout << "└──────────────────────────┘" << std::endl;
    }
}
