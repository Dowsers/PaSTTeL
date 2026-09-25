#ifndef MULTIPHASE_TEMPLATE_H
#define MULTIPHASE_TEMPLATE_H

#include <memory>
#include <vector>
#include "templates/ranking_template.h"
#include "termination/affine_function_generator.h"

/**
 * @brief Multiphase ranking function template (k phases, f_0 .. f_{k-1}).
 *
 * Models a loop that goes through phases 0, 1, ..., k-1 IN ORDER (phase i
 * only starts once phase i-1 is over, never the reverse) -- as opposed to
 * NestedTemplate (borrowing across all steps) or LexicographicTemplate
 * (simultaneous multi-component comparison).
 *
 * Canonical example: `while (x > 0) { x = x + y; y = y - 1; }` -- f_0 = y
 * (decreases unconditionally, no lower bound needed), f_1 = x (only needs to
 * decrease once y <= 0; while y > 0, x is free to grow).
 *
 * Positive conclusions (before negation):
 *
 * phi_decr_0        : f_0(x) - f_0(x') > delta_0                         (1 atom, no escape)
 * phi_decr_i (i>=1) : f_i(x) - f_i(x') > delta_i  OR  f_{i-1}(x) > 0     (2-atom disjunction)
 * phi_bound         : OR_i f_i(x) > 0, over EVERY phase i=0..k-1          (k-atom disjunction, all strict)
 *
 * phi_bound matches Ultimate LassoRanker's MultiphaseTemplate exactly (its
 * own doc comment states the same "\/_i f_i(x) > 0" over all phases, not
 * just the last one -- verified against
 * lassoranker/termination/templates/MultiphaseTemplate.java). Asserting only
 * f_{k-1}(x) >= 0 (an earlier, incorrect version of this template) leaves no
 * conclusion to refute at states where every f_i(x) <= 0 simultaneously,
 * which made Motzkin's certificate search spuriously UNSAT even for
 * textbook multiphase instances (e.g. Ultimate's own 3Phase.bpl, authored by
 * one of LassoRanker's own creators) that this template should solve.
 *
 * Soundness sketch: f_0 decreases every step with no exception, so it
 * eventually drops <=0 and stays there forever (monotonic). That permanently
 * removes phi_decr_1's escape, forcing f_1 to decrease every remaining step,
 * which by the same argument eventually strands f_2, and so on. f_{k-1}
 * ultimately must decrease forever while staying >=0 -- contradiction with
 * an infinite run.
 *
 * Local supporting invariants (phi1/phi2) and Motzkin context construction
 * are delegated to GenericTerminationSynthesizer, same pipeline as
 * AffineTemplate/NestedTemplate/LexicographicTemplate.
 */
class MultiphaseTemplate : public RankingTemplate {
public:
    explicit MultiphaseTemplate(int num_phases = 2, int delta_value = 0);

    void init(const LassoProgram& lasso) override;

    std::vector<ConclusionPart> getConstraintsDec(
        const std::vector<std::string>& in_vars,
        const std::vector<std::string>& out_vars) const override;

    std::vector<ConclusionPart> getConstraintsBounded(
        const std::vector<std::string>& in_vars) const override;

    /**
     * @brief Declares every phase's coefficients, and EVERY delta_params_[i]
     * with a strict `> delta_value_` assertion (all phases, not just the
     * first -- required for soundness of the escape atoms).
     */
    void declareParameters(SMTSolverInterface* solver) const override;

    TemplateParameters getParameters() const override;

    std::vector<RankingFunction> extractRankingFunctions(
        SMTSolverInterface* solver,
        const std::vector<std::string>& program_vars) const override;

    std::string getName() const override {
        return std::to_string(num_phases_) + "-multiphase";
    }

    std::string getDescription() const override;

    void printInfo() const override;

    int getNumPhases() const { return num_phases_; }

private:
    int num_phases_;
    int delta_value_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::unique_ptr<AffineFunctionGenerator>> generators_;
    std::vector<std::string> delta_params_;  // one delta per phase
};

#endif // MULTIPHASE_TEMPLATE_H
