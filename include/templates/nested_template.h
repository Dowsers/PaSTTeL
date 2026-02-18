#ifndef NESTED_TEMPLATE_H
#define NESTED_TEMPLATE_H

#include "templates/ranking_template.h"

/**
 * @brief Template Nested avec Supporting Invariants
 * 
 * Génère les contraintes BMS + Nested :
 * 
 * φ1: stem(x,x') → SI(x') ≥ 0                    (Initiation SI)
 * φ2: loop(x,x') → SI(x') - SI(x) ≥ 0            (Non-decreasing SI)
 * φ0: loop(x,x') → f₀(x) - f₀(x') - δ - Σ SI(x) > 0   (f₀ decreases with SI)
 * φᵢ: loop(x,x') → fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) - Σ SI(x) > 0  (fᵢ borrows from fᵢ₋₁ with SI)
 * φₙ: loop(x,x') → fₙ₋₁(x) ≥ 0                   (Boundedness)
 * 
 * Source: "Nested Interpolants" (Heizmann et al., POPL 2010)
 *         + BMS method pour les SI
 */
class NestedTemplate : public RankingTemplate {
public:
    /**
     * @brief Constructeur
     * @param num_components Nombre de composantes affines (k ≥ 2)
     * @param num_si_strict Nombre de SI stricts (>)
     * @param num_si_nonstrict Nombre de SI non-stricts (≥)
     */
    NestedTemplate(int num_components = 2,
                   int num_si_strict = 0, 
                   int num_si_nonstrict = 1,
                   int delta_value = 1);
    
    // ========================================================================
    // IMPLÉMENTATION DE L'INTERFACE RankingTemplate
    // ========================================================================
    
    void init(const LassoProgram& lasso) override;
    
    std::vector<MotzkinContext> getConstraints() const override;
    
    TemplateParameters getParameters() const override;
    
    std::string getName() const override {
        return std::to_string(num_components_) + "-nested";
    }
    
    std::string getDescription() const override;
    
    int getNumSupportingInvariants() const override {
        return num_supporting_invariants_;
    }
    
    bool supportsStrictInvariants() const override {
        return true;
    }
    
    void printInfo() const override;
    
    // ========================================================================
    // MÉTHODES SPÉCIFIQUES
    // ========================================================================
    
    int getNumComponents() const { return num_components_; }
    int getNumStrictInvariants() const { return num_strict_invariants_; }
    int getNumNonStrictInvariants() const { return num_nonstrict_invariants_; }
    
private:
    // Configuration
    int num_components_;           // k composantes (≥ 2)
    int num_strict_invariants_;
    int num_nonstrict_invariants_;
    int num_supporting_invariants_;  // Total
    
    // État après init()
    LassoProgram lasso_;
    bool initialized_;
    
    // Paramètres SMT
    std::vector<std::vector<std::string>> component_params_;  // Paramètres pour chaque fᵢ
    std::vector<std::vector<std::string>> si_params_;         // Paramètres pour chaque SI
    std::string delta_param_;                                 // Nommageδ unique
    int delta_value_;                                         // Valeur de δ
    // ========================================================================
    // GÉNÉRATION DES CONTRAINTES
    // ========================================================================
    
    /**
     * @brief Génère φ1: stem → SI(x') ≥ 0 pour chaque SI
     */
    std::vector<MotzkinContext> generatePhi1_StemInitiation() const;
    
    /**
     * @brief Génère φ2: loop → SI(x') - SI(x) ≥ 0 pour chaque SI
     */
    std::vector<MotzkinContext> generatePhi2_LoopConsecution() const;
    
    /**
     * @brief Génère φ0: loop → f₀(x) - f₀(x') - δ - Σ SI(x) > 0
     */
    std::vector<MotzkinContext> generatePhi0_Decrement0() const;
    
    /**
     * @brief Génère φᵢ: loop → fᵢ(x) - fᵢ(x') + fᵢ₋₁(x) - Σ SI(x) > 0  (i > 0)
     */
    std::vector<MotzkinContext> generatePhiI_DecrementI(int i) const;
    
    /**
     * @brief Génère φₙ: loop → fₙ₋₁(x) ≥ 0
     */
    std::vector<MotzkinContext> generatePhiN_Boundedness() const;
    
    // ========================================================================
    // CONSTRUCTION DES TERMES
    // ========================================================================
    
    /**
     * @brief Construit fᵢ(vars) avec component_params_[i]
     */
    LinearInequality buildComponent(int idx, const std::vector<std::string>& vars) const;
    
    /**
     * @brief Construit SI_i(vars) avec si_params[i]
     */
    LinearInequality buildSupportingInvariant(
        int si_index,
        const std::vector<std::string>& vars,
        bool is_strict) const;
    
    /**
     * @brief Initialise les noms des paramètres SMT
     */
    void initializeParameters();
};

#endif // NESTED_TEMPLATE_H