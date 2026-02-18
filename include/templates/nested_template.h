#ifndef NESTED_TEMPLATE_H
#define NESTED_TEMPLATE_H

#include "templates/ranking_template.h"

/**
 * @brief Template Nested pour ranking functions à k composantes
 *
 * Génère les contraintes RF (sans SI) :
 * φ0: loop(x,x') ∧ Σ SI(x) ≥ 0 → f₀(x) - f₀(x') ≥ δ
 * φᵢ: loop(x,x') ∧ Σ SI(x) ≥ 0 → fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) > 0  (i > 0)
 * φₙ: loop(x,x') ∧ Σ SI(x) ≥ 0 → fₙ₋₁(x) ≥ 0
 *
 * Les SI (φ1/φ2) sont gérés par SupportingInvariantGenerator.
 */
class NestedTemplate : public RankingTemplate {
public:
    explicit NestedTemplate(int num_components = 2, int delta_value = 1);

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

    std::string getName() const override {
        return std::to_string(num_components_) + "-nested";
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
    std::string delta_param_;

    // ========================================================================
    // GÉNÉRATION DES CONTRAINTES RF
    // ========================================================================

    std::vector<MotzkinContext> generatePhi0_Decrement0(
        const std::vector<LinearInequality>& si_preconditions) const;

    std::vector<MotzkinContext> generatePhiI_DecrementI(
        int i,
        const std::vector<LinearInequality>& si_preconditions) const;

    std::vector<MotzkinContext> generatePhiN_Boundedness(
        const std::vector<LinearInequality>& si_preconditions) const;

    LinearInequality buildComponent(int idx, const std::vector<std::string>& vars) const;

    void initializeParameters();
};

#endif // NESTED_TEMPLATE_H
