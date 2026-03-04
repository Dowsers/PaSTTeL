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
    const auto& eff_vars = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
    int n = static_cast<int>(eff_vars.size());
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
    std::shared_ptr<SMTSolver> solver) const
{
    for (const auto& gen : generators_) {
        gen->declareParameters(solver);
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
// PREMISSES POUR LES TEMPLATES RF
// ============================================================================

std::vector<LinearInequality> SupportingInvariantGenerator::buildPreconditions(
    const std::vector<std::string>& vars) const
{
    std::vector<LinearInequality> preconditions;
    for (int k = 0; k < num_si_; ++k) {
        LinearInequality li = buildSI(k, vars);
        li.strict = isStrict(k);
        // motzkin_coef = ANYTHING (default from generate())
        preconditions.push_back(li);
    }
    return preconditions;
}

// ============================================================================
// phi1 : STEM INITIATION -- stem(x,x') -> SI(x') >= 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
SupportingInvariantGenerator::generatePhi1() const
{
    std::vector<RankingTemplate::MotzkinContext> contexts;

    for (int k = 0; k < num_si_; ++k) {
        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.stem.polyhedra) {
            RankingTemplate::MotzkinContext ctx;
            ctx.annotation = "phi1: SI_" + std::to_string(k)
                           + " initiation (poly " + std::to_string(poly_idx) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            std::vector<std::string> stem_out_vars;
            const auto& eff_vars_phi1 = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
            for (const auto& var : eff_vars_phi1) {
                stem_out_vars.push_back(lasso_.stem.getSSAVar(var, true));
            }

            // neg(SI(x') >= 0) = -SI(x') > 0  (or -SI(x') >= 0 if SI is strict)
            LinearInequality neg_si = buildSI(k, stem_out_vars);
            neg_si.negate();
            neg_si.strict = !isStrict(k);
            neg_si.motzkin_coef = LinearInequality::ONE;

            ctx.constraints.push_back(neg_si);
            contexts.push_back(ctx);
            poly_idx++;
        }
    }

    return contexts;
}

// ============================================================================
// phi2 : LOOP CONSECUTION -- SI(x) /\ loop(x,x') -> SI(x') >= 0
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
SupportingInvariantGenerator::generatePhi2() const
{
    std::vector<RankingTemplate::MotzkinContext> contexts;

    for (int k = 0; k < num_si_; ++k) {
        int poly_idx = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            RankingTemplate::MotzkinContext ctx;
            ctx.annotation = "phi2: SI_" + std::to_string(k)
                           + " consecution (poly " + std::to_string(poly_idx) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            std::vector<std::string> loop_in_vars, loop_out_vars;
            const auto& eff_vars_phi2 = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
            for (const auto& var : eff_vars_phi2) {
                loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
                loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
            }

            // Premise : SI(x) >= 0 (or > 0 if strict)
            LinearInequality si_precond = buildSI(k, loop_in_vars);
            si_precond.strict = isStrict(k);
            si_precond.motzkin_coef = LinearInequality::ANYTHING;
            ctx.constraints.push_back(si_precond);

            // Negated conclusion : neg(SI(x') >= 0)
            LinearInequality neg_si_prime = buildSI(k, loop_out_vars);
            neg_si_prime.negate();
            neg_si_prime.strict = !isStrict(k);
            neg_si_prime.motzkin_coef = LinearInequality::ZERO_AND_ONE;  // critical
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
