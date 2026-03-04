#include <cmath>
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
    , delta_param_("DELTA")
    , initialized_(false)
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
    const auto& vars = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
    int n = static_cast<int>(vars.size());

    generators_.clear();
    for (int i = 0; i < num_components_; ++i) {
        generators_.push_back(
            std::make_unique<AffineFunctionGenerator>("RANK_" + std::to_string(i), n));
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

void NestedTemplate::declareParameters(std::shared_ptr<SMTSolver> solver) const {
    if (!initialized_) {
        throw std::runtime_error("NestedTemplate::declareParameters() called before init()");
    }
    for (const auto& gen : generators_) {
        gen->declareParameters(solver);
    }
    solver->declareVariable(delta_param_, "Real");
    solver->addAssertion("(> " + delta_param_ + " " + std::to_string(delta_value_) + ")");
}

// ============================================================================
// RETOURNE LES PARAMETRES (pour extractParameters dans le synthesizer)
// ============================================================================

RankingTemplate::TemplateParameters NestedTemplate::getParameters() const {
    if (!initialized_) {
        throw std::runtime_error("NestedTemplate::getParameters() called before init()");
    }
    TemplateParameters params;
    for (const auto& gen : generators_) {
        const auto& names = gen->getParamNames();
        params.ranking_params.insert(params.ranking_params.end(), names.begin(), names.end());
    }
    params.delta_param = delta_param_;
    params.delta_value = delta_value_;
    return params;
}

// ============================================================================
// CONCLUSIONS POSITIVES DE DECROISSANCE 
// ============================================================================

std::vector<LinearInequality> NestedTemplate::getConstraintsDec(
    const std::vector<std::string>& in_vars,
    const std::vector<std::string>& out_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("NestedTemplate::getConstraintsDec() called before init()");
    }

    std::vector<LinearInequality> result;

    // i=0 : f0(x) - f0(x') - delta > 0
    {
        LinearInequality li = generators_[0]->generate(in_vars);
        LinearInequality li2 = generators_[0]->generate(out_vars);
        li2.negate();
        li = li + li2;
        li.constant.coefficients[delta_param_] -= 1.0;
        li.strict = true;
        li.motzkin_coef = LinearInequality::ONE;
        result.push_back(li);
    }

    // i>0 : fi(x) - fi(x') + f_{i-1}(x) > 0
    for (int i = 1; i < num_components_; ++i) {
        LinearInequality li = generators_[i]->generate(in_vars);
        LinearInequality li2 = generators_[i]->generate(out_vars);
        li2.negate();
        li = li + li2;
        LinearInequality li3 = generators_[i - 1]->generate(in_vars);
        li = li + li3;
        li.strict = true;
        li.motzkin_coef = LinearInequality::ONE;
        result.push_back(li);
    }

    return result;
}

// ============================================================================
// CONCLUSION POSITIVE DE BORNAGE : f_{n-1}(x) > 0
// ============================================================================

LinearInequality NestedTemplate::getConstraintsBounded(
    const std::vector<std::string>& in_vars) const
{
    if (!initialized_) {
        throw std::runtime_error("NestedTemplate::getConstraintsBounded() called before init()");
    }

    LinearInequality li = generators_[num_components_ - 1]->generate(in_vars);
    li.strict = true;
    li.motzkin_coef = LinearInequality::ONE;
    return li;
}

// ============================================================================
// EXTRACTION DES RESULTATS
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
        auto values = generators_[i]->extractValues(solver);
        for (size_t j = 0; j < n && j < values.size(); ++j) {
            rf.coefficients[program_vars[j]] = static_cast<int64_t>(std::round(values[j]));
        }
        if (values.size() > n) {
            rf.constant = static_cast<int64_t>(std::round(values[n]));
        }
        rf.delta = static_cast<int64_t>(std::round(delta));
        components.push_back(rf);
    }
    return components;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

std::string NestedTemplate::getDescription() const {
    std::ostringstream oss;
    oss << num_components_ << "-nested: f0 decreases, each fi can borrow from f_{i-1}";
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
