#ifndef LEXICOGRAPHIC_TEMPLATE_H
#define LEXICOGRAPHIC_TEMPLATE_H

#include "templates/ranking_template.h"

/**
 * @brief Template Lexicographique pour ranking functions a k composantes
 *
 * Uses a vector of k affine functions (f0, f1, ..., fk-1) that must decrease
 * in lexicographic order at every loop iteration.
 *
 * Obligations (see getConstraintsBounded/getConstraintsDec below for how each
 * maps onto the generic ConclusionPart / OR-of-atoms representation):
 *
 * phi_bound_i: loop(x,x') -> fi(x) > 0                          (k parts, 1 atom each)
 *
 * phi_consec_i (i < k-1): loop(x,x') ->
 *   fi(x') <= fi(x)  OR  exists j<i : fj(x) - fj(x') > dj       (k-1 parts, 1+i atoms each)
 *
 * phi_decrement: loop(x,x') -> exists i : fi(x) - fi(x') > di   (1 part, k atoms)
 *
 * Source: "Lexicographic Ranking Functions" (Ultimate LassoRanker).
 *
 * Local supporting invariants (phi1/phi2) and the Motzkin context construction
 * are delegated to GenericTerminationSynthesizer -- same pipeline as
 * AffineTemplate/NestedTemplate, thanks to the OR-of-atoms generalization of
 * getConstraintsDec/getConstraintsBounded.
 */
class LexicographicTemplate : public RankingTemplate {
public:
    explicit LexicographicTemplate(int num_components = 2, int delta_value = 0);

    // Interface RankingTemplate
    void init(const LassoProgram& lasso) override;

    std::vector<ConclusionPart> getConstraintsDec(
        const std::vector<std::string>& in_vars,
        const std::vector<std::string>& out_vars) const override;

    std::vector<ConclusionPart> getConstraintsBounded(
        const std::vector<std::string>& in_vars) const override;

    /**
     * @brief Declares ranking coefficients as Real, and EVERY delta_params_[i]
     * as Real with a strict `> delta_value_` assertion (all components, not
     * just the first -- required for soundness of phi_consec/phi_decrement).
     */
    void declareParameters(SMTSolverInterface* solver) const override;

    TemplateParameters getParameters() const override;

    std::vector<RankingFunction> extractRankingFunctions(
        SMTSolverInterface* solver,
        const std::vector<std::string>& program_vars) const override;

    std::string getName() const override {
        return std::to_string(num_components_) + "-lex";
    }

    std::string getDescription() const override;

    void printInfo() const override;

    int getNumComponents() const { return num_components_; }

private:
    int num_components_;
    int delta_value_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::vector<std::string>> component_params_;
    std::vector<std::string> delta_params_;  // one delta per component

    // Term building
    LinearInequality buildComponent(int idx, const std::vector<std::string>& vars) const;
    // fi(in_vars) - fi(out_vars), as a plain LinearInequality (strict/motzkin_coef left default)
    LinearInequality buildComponentDiff(
        int idx,
        const std::vector<std::string>& in_vars,
        const std::vector<std::string>& out_vars) const;
    void initializeParameters();
};

#endif // LEXICOGRAPHIC_TEMPLATE_H
