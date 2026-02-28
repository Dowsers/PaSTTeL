#include "termination/affine_function_generator.h"

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

AffineFunctionGenerator::AffineFunctionGenerator(
    const std::string& prefix, int num_vars)
    : prefix_(prefix)
    , num_vars_(num_vars)
{
    for (int i = 0; i < num_vars_; ++i) {
        param_names_.push_back(prefix_ + "_" + std::to_string(i));
    }
    param_names_.push_back(prefix_ + "_const");
}

// ============================================================================
// DÉCLARATION DES PARAMÈTRES SMT
// ============================================================================

void AffineFunctionGenerator::declareParameters(
    std::shared_ptr<SMTSolver> solver) const
{
    for (const auto& name : param_names_) {
        solver->declareVariable(name, "Real");
    }
}

// ============================================================================
// GÉNÉRATION DE L'EXPRESSION AFFINE
// ============================================================================

LinearInequality AffineFunctionGenerator::generate(
    const std::vector<std::string>& vars) const
{
    LinearInequality result;
    result.strict = false;
    result.motzkin_coef = LinearInequality::ANYTHING;

    for (int i = 0; i < num_vars_; ++i) {
        AffineTerm coef;
        coef.coefficients[param_names_[i]] = 1.0;
        coef.constant = 0.0;
        result.setCoefficient(vars[i], coef);
    }

    AffineTerm const_term;
    const_term.coefficients[param_names_.back()] = 1.0;
    const_term.constant = 0.0;
    result.constant = const_term;

    return result;
}

// ============================================================================
// ACCESSEURS
// ============================================================================

const std::vector<std::string>& AffineFunctionGenerator::getParamNames() const {
    return param_names_;
}

std::vector<double> AffineFunctionGenerator::extractValues(
    std::shared_ptr<SMTSolver> solver) const
{
    std::vector<double> values;
    for (const auto& p : param_names_) {
        values.push_back(solver->getValue(p));
    }
    return values;
}

std::vector<std::pair<int64_t, int64_t>> AffineFunctionGenerator::extractRationals(
    std::shared_ptr<SMTSolver> solver) const
{
    std::vector<std::pair<int64_t, int64_t>> rationals;
    for (const auto& p : param_names_) {
        rationals.push_back(solver->getRationalValue(p));
    }
    return rationals;
}
