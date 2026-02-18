#ifndef AFFINE_TEMPLATE_REFACTORED_H
#define AFFINE_TEMPLATE_REFACTORED_H

#include "templates/ranking_template.h"

/**
 * @brief Template Affine pour ranking functions linéaires
 *
 * Génère les contraintes RF (sans SI) :
 * φ3: loop(x,x') ∧ Σ SI(x) ≥ 0 → f(x) - f(x') ≥ δ   (Decrement)
 * φ4: loop(x,x') ∧ Σ SI(x) ≥ 0 → f(x) ≥ 0             (Boundedness)
 *
 * Les SI (φ1/φ2) sont gérés par SupportingInvariantGenerator.
 * Les si_preconditions sont injectées via getConstraints(si_preconditions).
 */
class AffineTemplate : public RankingTemplate {
public:
    explicit AffineTemplate(int delta_value = 1);

    // ========================================================================
    // IMPLÉMENTATION DE L'INTERFACE RankingTemplate
    // ========================================================================

    void init(const LassoProgram& lasso) override;

    std::vector<MotzkinContext> getConstraints(
        const std::vector<LinearInequality>& si_preconditions = {}) const override;

    TemplateParameters getParameters() const override;

    std::vector<RankingFunction> extractRankingFunctions(
        std::shared_ptr<SMTSolver> solver,
        const std::vector<std::string>& program_vars) const override;

    std::string getName() const override { return "Affine"; }

    std::string getDescription() const override {
        return "Linear ranking function: f(x) = c⊺·x + c₀";
    }

    void printInfo() const override;

private:
    int delta_value_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::string> ranking_params_;
    std::string delta_param_;

    // ========================================================================
    // GÉNÉRATION DES CONTRAINTES RF
    // ========================================================================

    std::vector<MotzkinContext> generatePhi3_RankingDecrement(
        const std::vector<LinearInequality>& si_preconditions) const;

    std::vector<MotzkinContext> generatePhi4_RankingBoundedness(
        const std::vector<LinearInequality>& si_preconditions) const;

    LinearInequality buildRankingFunction(const std::vector<std::string>& vars) const;

    void initializeParameters();
};

#endif // AFFINE_TEMPLATE_REFACTORED_H
