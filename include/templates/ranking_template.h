#ifndef RANKING_TEMPLATE_H
#define RANKING_TEMPLATE_H

#include <vector>
#include <string>
#include <memory>

#include "lasso_program.h"
#include "linear_inequality.h"


/**
 * @brief Interface abstraite pour tous les templates de ranking functions
 * 
 * Architecture modulaire permettant :
 * - Ajout facile de nouveaux templates (Affine, Nested, Lexicographic, etc.)
 * - Exécution parallèle de plusieurs templates
 * - Séparation claire entre génération de contraintes et résolution SMT
 */
class RankingTemplate {
public:
    /**
     * @brief Contexte Motzkin - une contrainte universelle à vérifier
     * Représente: ∀x,x' (premises → conclusion)
     * Transformé par Motzkin en: ∃λ₁,...,λₙ ≥ 0 (Σ λᵢ·premiseᵢ - conclusion ≥ 0)
     */
    struct MotzkinContext {
        std::vector<LinearInequality> constraints;  // Toutes contraintes (premises + négation conclusion)
        std::string annotation;                     // Description pour debug/logging
        
        MotzkinContext() = default;
        MotzkinContext(const std::vector<LinearInequality>& c, const std::string& a)
            : constraints(c), annotation(a) {}
    };
    
    /**
     * @brief Informations sur les paramètres du template pour SMT
     */
    struct TemplateParameters {
        std::vector<std::string> ranking_params;    // Paramètres de la ranking function
        std::vector<std::string> si_params;         // Paramètres des supporting invariants
        std::vector<bool> si_is_strict;             // true = SI strict (>), false = non-strict (≥)
        std::string delta_param;                    // Nom du paramètre δ
        int delta_value;                            // Valeur de δ
        
        int getTotalParameterCount() const {
            return ranking_params.size() + si_params.size() + (delta_param.empty() ? 0 : 1);
        }
    };
    
    virtual ~RankingTemplate() = default;
    
    // ========================================================================
    // MÉTHODES ABSTRAITES - À implémenter par chaque template
    // ========================================================================
    
    /**
     * @brief Initialise le template avec le programme lasso
     * DOIT être appelé avant getConstraints()
     */
    virtual void init(const LassoProgram& lasso) = 0;
    
    /**
     * @brief Génère tous les contextes Motzkin (contraintes BMS)
     * @return Liste de contextes, un par contrainte universelle
     */
    virtual std::vector<MotzkinContext> getConstraints() const = 0;
    
    /**
     * @brief Retourne les informations sur les paramètres SMT
     */
    virtual TemplateParameters getParameters() const = 0;
    
    /**
     * @brief Retourne le nom du template (pour logging)
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Retourne une description du template
     */
    virtual std::string getDescription() const = 0;
    
    // ========================================================================
    // MÉTHODES OPTIONNELLES - Peuvent être override si nécessaire
    // ========================================================================
    
    /**
     * @brief Retourne le nombre de supporting invariants
     */
    virtual int getNumSupportingInvariants() const { return 0; }
    
    /**
     * @brief Indique si le template supporte les SI stricts
     */
    virtual bool supportsStrictInvariants() const { return false; }
    
    /**
     * @brief Valide la configuration du template
     * @return true si la configuration est valide
     */
    virtual bool validateConfiguration() const { return true; }
    
    /**
     * @brief Affiche les informations du template
     */
    virtual void printInfo() const = 0;
};

#endif // RANKING_TEMPLATE_H