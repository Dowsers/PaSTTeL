#ifndef NESTED_TEMPLATE_H
#define NESTED_TEMPLATE_H

#include <memory>
#include <vector>
#include "templates/ranking_template.h"
#include "termination/affine_function_generator.h"

/**
 * @brief Template Nested pour ranking functions a k composantes
 *
 * Genere les conclusions positives (avant negation) :
 * i=0 : f0(x) - f0(x') - delta > 0            (strict)
 * i>0 : fi(x) - fi(x') + f_{i-1}(x) > 0       (strict)
 * bound: f_{n-1}(x) >= 0                        (non-strict)
 *
 * Les SI (phi1/phi2) et la construction des contextes Motzkin sont
 * deleguees a GenericTerminationSynthesizer::buildConstraints().
 */
class NestedTemplate : public RankingTemplate {
public:
    explicit NestedTemplate(int num_components = 2, int delta_value = 0);

    // ========================================================================
    // INTERFACE RankingTemplate -- methodes abstraites
    // ========================================================================

    void init(const LassoProgram& lasso) override;

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

    // ========================================================================
    // NOUVELLE INTERFACE
    // ========================================================================

    /**
     * @brief num_components conclusions positives de decroissance (non negees).
     *
     * i=0 : f0(x) - f0(x')  >= delta   (strict=false, ONE)
     * i>0 : fi(x) - fi(x') + f_{i-1}(x) > 0  (strict=true, ONE)
     */
    std::vector<LinearInequality> getConstraintsDec(
        const std::vector<std::string>& in_vars,
        const std::vector<std::string>& out_vars) const override;

    /**
     * @brief Conclusion positive de bornage : f_{n-1}(x) >= 0
     * (strict=false, motzkin_coef=ONE)
     */
    LinearInequality getConstraintsBounded(
        const std::vector<std::string>& in_vars) const override;

    /**
     * @brief Declare les parametres SMT (tous coefficients + delta) dans le solveur
     */
    void declareParameters(std::shared_ptr<SMTSolver> solver) const override;

private:
    int num_components_;
    int delta_value_;
    std::string delta_param_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::unique_ptr<AffineFunctionGenerator>> generators_;
};

#endif // NESTED_TEMPLATE_H
