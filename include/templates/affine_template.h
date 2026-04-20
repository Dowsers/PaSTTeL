#ifndef AFFINE_TEMPLATE_REFACTORED_H
#define AFFINE_TEMPLATE_REFACTORED_H

#include <memory>
#include "templates/ranking_template.h"
#include "termination/affine_function_generator.h"

/**
 * @brief Template Affine pour ranking functions lineaires
 *
 * Genere les conclusions positives (avant negation) :
 * dec  : f(x) - f(x') - delta > 0   (strict)
 * bound: f(x) >= 0                   (non-strict)
 *
 * Les SI (phi1/phi2) et la construction des contextes Motzkin sont
 * deleguees a GenericTerminationSynthesizer::buildConstraints().
 */
class AffineTemplate : public RankingTemplate {
public:
    explicit AffineTemplate(int delta_value = 0);

    // ========================================================================
    // INTERFACE RankingTemplate -- methodes abstraites
    // ========================================================================

    void init(const LassoProgram& lasso) override;

    TemplateParameters getParameters() const override;

    std::vector<RankingFunction> extractRankingFunctions(
        std::shared_ptr<SMTSolver> solver,
        const std::vector<std::string>& program_vars) const override;

    std::string getName() const override { return "Affine"; }

    std::string getDescription() const override {
        return "Linear ranking function: f(x) = c*x + c0";
    }

    void printInfo() const override;

    // ========================================================================
    // NOUVELLE INTERFACE
    // ========================================================================

    /**
     * @brief Conclusion positive de decroissance : f(x) - f(x')  >= delta
     * (strict=false, motzkin_coef=ONE)
     */
    std::vector<LinearInequality> getConstraintsDec(
        const std::vector<std::string>& in_vars,
        const std::vector<std::string>& out_vars) const override;

    /**
     * @brief Conclusion positive de bornage : f(x) >= 0
     * (strict=false, motzkin_coef=ONE)
     */
    LinearInequality getConstraintsBounded(
        const std::vector<std::string>& in_vars) const override;

    /**
     * @brief Declare les parametres SMT (coefficients + delta) dans le solveur
     */
    void declareParameters(std::shared_ptr<SMTSolver> solver) const override;

private:
    int delta_value_;
    std::string delta_param_;

    LassoProgram lasso_;
    bool initialized_;

    std::unique_ptr<AffineFunctionGenerator> generator_;
};

#endif // AFFINE_TEMPLATE_REFACTORED_H
