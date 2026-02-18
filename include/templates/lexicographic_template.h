#ifndef LEXICOGRAPHIC_TEMPLATE_H
#define LEXICOGRAPHIC_TEMPLATE_H

#include "templates/ranking_template.h"

/**
 * @brief Template Lexicographique avec Supporting Invariants
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
 * Source: "Lexicographic Ranking Functions" (Ultimate LassoRanker)
 */
class LexicographicTemplate : public RankingTemplate {
public:
    LexicographicTemplate(int num_components = 2,
                          int num_si_strict = 0,
                          int num_si_nonstrict = 0,
                          int delta_value = 1);

    // Interface RankingTemplate
    void init(const LassoProgram& lasso) override;
    std::vector<MotzkinContext> getConstraints() const override;
    TemplateParameters getParameters() const override;

    std::string getName() const override {
        return std::to_string(num_components_) + "-lex";
    }

    std::string getDescription() const override;

    int getNumSupportingInvariants() const override {
        return num_supporting_invariants_;
    }

    bool supportsStrictInvariants() const override {
        return true;
    }

    void printInfo() const override;

    int getNumComponents() const { return num_components_; }

private:
    int num_components_;
    int num_strict_invariants_;
    int num_nonstrict_invariants_;
    int num_supporting_invariants_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::vector<std::string>> component_params_;
    std::vector<std::string> delta_params_;  // one delta per component
    std::vector<std::vector<std::string>> si_params_;
    int delta_value_;

    // Constraint generation
    std::vector<MotzkinContext> generateBoundedness() const;
    std::vector<MotzkinContext> generateConsecution() const;
    std::vector<MotzkinContext> generateDecrement() const;

    // Term building
    LinearInequality buildComponent(int idx, const std::vector<std::string>& vars) const;
    LinearInequality buildSupportingInvariant(int si_index, const std::vector<std::string>& vars, bool is_strict) const;
    void initializeParameters();
};

#endif // LEXICOGRAPHIC_TEMPLATE_H
