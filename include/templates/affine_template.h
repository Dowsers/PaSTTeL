#ifndef AFFINE_TEMPLATE_REFACTORED_H
#define AFFINE_TEMPLATE_REFACTORED_H

#include "templates/ranking_template.h"

/**
 * @brief Template Affine pour ranking functions linéaires (BMS Method)
 * 
 * Génère les 4 contraintes BMS :
 * φ1: stem(x,x') → SI(x') ≥ 0                    (Initiation)
 * φ2: loop(x,x') → SI(x') - SI(x) ≥ 0            (Non-decreasing SI)
 * φ3: loop(x,x') → f(x) - f(x') - SI(x) ≥ δ      (Decrement)
 * φ4: loop(x,x') → f(x) ≥ 0                      (Boundedness)
 * 
 * où f(x) = c⊺·x + c₀ est la ranking function
 *     SI(x) = s⊺·x + s₀ est un supporting invariant
 */
class AffineTemplate : public RankingTemplate {
public:
    /**
     * @brief Constructeur
     * @param num_si_strict Nombre de SI stricts (>)
     * @param num_si_nonstrict Nombre de SI non-stricts (≥)
     */
    AffineTemplate(int num_si_strict = 0, int num_si_nonstrict = 1, int delta_value = 1);
    
    // ========================================================================
    // IMPLÉMENTATION DE L'INTERFACE RankingTemplate
    // ========================================================================
    
    void init(const LassoProgram& lasso) override;
    
    std::vector<MotzkinContext> getConstraints() const override;
    
    TemplateParameters getParameters() const override;
    
    std::string getName() const override {
        return "Affine";
    }
    
    std::string getDescription() const override {
        return "Linear ranking function: f(x) = c⊺·x + c₀";
    }
    
    int getNumSupportingInvariants() const override {
        return num_supporting_invariants_;
    }
    
    bool supportsStrictInvariants() const override {
        return true;
    }
    
    void printInfo() const override;
    
    // ========================================================================
    // MÉTHODES SPÉCIFIQUES À AFFINE
    // ========================================================================
    
    /**
     * @brief Retourne le nombre de SI stricts
     */
    int getNumStrictInvariants() const { return num_strict_invariants_; }
    
    /**
     * @brief Retourne le nombre de SI non-stricts
     */
    int getNumNonStrictInvariants() const { return num_nonstrict_invariants_; }
    
private:
    // Configuration
    int num_strict_invariants_;
    int num_nonstrict_invariants_;
    int num_supporting_invariants_;  // Total

    int delta_value_;                 // Valeur de δ
    
    // État après init()
    LassoProgram lasso_;
    bool initialized_;
    
    // Paramètres SMT générés
    std::vector<std::string> ranking_params_;
    std::vector<std::vector<std::string>> si_params_;  // Un vecteur par SI
    std::string delta_param_;
    
    // ========================================================================
    // GÉNÉRATION DES CONTRAINTES BMS
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
     * @brief Génère φ3: loop → f(x) - f(x') ≥ δ + Σ SI(x)
     */
    std::vector<MotzkinContext> generatePhi3_RankingDecrement() const;
    
    /**
     * @brief Génère φ4: loop → f(x) ≥ 0
     */
    std::vector<MotzkinContext> generatePhi4_RankingBoundedness() const;
    
    // ========================================================================
    // CONSTRUCTION DES TERMES
    // ========================================================================
    
    /**
     * @brief Construit f(vars) avec ranking_params
     */
    LinearInequality buildRankingFunction(const std::vector<std::string>& vars) const;
    
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

#endif // AFFINE_TEMPLATE_REFACTORED_H