#ifndef LEXICOGRAPHIC_TEMPLATE_H
#define LEXICOGRAPHIC_TEMPLATE_H

#include "templates/ranking_template.h"

/**
 * @brief Template Lexicographique pour ranking functions à k composantes
 *
 * Utilise un vecteur de k fonctions affines (f0, f1, ..., fk-1)
 * qui doivent decroitre en ordre lexicographique a chaque iteration.
 *
 * Contraintes:
 *
 * phi_bound: loop(x,x') -> fi(x) > 0                         (k contraintes)
 *
 * phi_consec_i (i < k-1): loop(x,x') ->
 *   fi(x') <= fi(x)  OR  exists j<i : fj(x) - fj(x') > dj   (k-1 contraintes)
 *
 * phi_decrement: loop(x,x') -> exists i : fi(x) - fi(x') > di (1 contrainte)
 *
 * Les SI (φ1/φ2) sont gérés par SupportingInvariantGenerator.
 * Les si_preconditions sont injectées via getConstraints(si_preconditions).
 *
 */
class LexicographicTemplate : public RankingTemplate {
public:
    explicit LexicographicTemplate(int num_components = 2, int delta_value = 0);

    // Interface RankingTemplate
    void init(const LassoProgram& lasso) override;

    std::vector<MotzkinContext> getConstraints(
        const std::vector<LinearInequality>& si_preconditions = {}) const override;

    TemplateParameters getParameters() const override;

    std::vector<RankingFunction> extractRankingFunctions(
        std::shared_ptr<SMTSolver> solver,
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

    // Constraint generation
    std::vector<MotzkinContext> generateBoundedness() const;
    std::vector<MotzkinContext> generateConsecution() const;
    std::vector<MotzkinContext> generateDecrement() const;

    // Term building
    LinearInequality buildComponent(int idx, const std::vector<std::string>& vars) const;
    void initializeParameters();
};

#endif // LEXICOGRAPHIC_TEMPLATE_H
