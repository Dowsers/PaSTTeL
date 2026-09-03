#include "termination/supporting_invariant_generator.h"

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

SupportingInvariantGenerator::SupportingInvariantGenerator(
    int num_si_strict, int num_si_nonstrict, int instance_id)
    : num_strict_(num_si_strict)
    , num_nonstrict_(num_si_nonstrict)
    , num_si_(num_si_strict + num_si_nonstrict)
    , instance_id_(instance_id)
    , initialized_(false)
{}

// ============================================================================
// INITIALISATION
// ============================================================================

void SupportingInvariantGenerator::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    initializeGenerators();
    initialized_ = true;
}

void SupportingInvariantGenerator::initializeGenerators() {
    int n = static_cast<int>(lasso_.program_vars.size());
    generators_.clear();
    for (int k = 0; k < num_si_; ++k) {
        // Si instance_id_ >= 0 : "SUP_INVAR_<instance_id>_<k>"  (SIG local, unique)
        // Sinon               : "SUP_INVAR_<k>"                  (ancien schema, compatibilite)
        std::string prefix = (instance_id_ >= 0)
            ? "SUP_INVAR_" + std::to_string(instance_id_) + "_" + std::to_string(k)
            : "SUP_INVAR_" + std::to_string(k);
        generators_.push_back(std::make_unique<AffineFunctionGenerator>(prefix, n));
    }
}

// ============================================================================
// DECLARATION DES PARAMETRES SMT
// ============================================================================

void SupportingInvariantGenerator::declareParameters(
    SMTSolverInterface* solver) const
{
    auto phantom_mask = lasso_.loopPhantomVarMask();
    for (const auto& gen : generators_) {
        gen->declareParameters(solver, phantom_mask);
    }
}

// ============================================================================
// CONSTRUCTION D'UN TERME SI (PUBLIC -- pour buildConstraints du synthesizer)
// ============================================================================

LinearInequality SupportingInvariantGenerator::buildSI(
    int si_idx,
    const std::vector<std::string>& vars) const
{
    // generate() retourne strict=false, motzkin_coef=ANYTHING par defaut
    return generators_[si_idx]->generate(vars);
}

// ============================================================================
// ACCESSEURS POUR L'EXTRACTION
// ============================================================================

std::vector<std::string> SupportingInvariantGenerator::getSIParams() const {
    std::vector<std::string> flat;
    for (const auto& gen : generators_) {
        const auto& names = gen->getParamNames();
        flat.insert(flat.end(), names.begin(), names.end());
    }
    return flat;
}

std::vector<bool> SupportingInvariantGenerator::getSIIsStrict() const {
    std::vector<bool> result;
    for (int k = 0; k < num_si_; ++k) {
        result.push_back(isStrict(k));
    }
    return result;
}
