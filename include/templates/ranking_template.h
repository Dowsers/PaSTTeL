#ifndef RANKING_TEMPLATE_H
#define RANKING_TEMPLATE_H

#include <vector>
#include <string>
#include <memory>

#include "lasso_program.h"
#include "linear_inequality.h"
#include "termination/ranking_function.h"


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
     * (uniquement les paramètres de la ranking function — les SI sont dans SupportingInvariantGenerator)
     */
    struct TemplateParameters {
        std::vector<std::string> ranking_params;    // Paramètres de la ranking function
        std::string delta_param;                    // Nom du paramètre δ
        int delta_value;                            // Valeur de δ

        int getTotalParameterCount() const {
            return ranking_params.size() + (delta_param.empty() ? 0 : 1);
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
     * @brief Génère les contextes Motzkin des contraintes de la ranking function
     *
     * Les φ1/φ2 (SI) sont générés par SupportingInvariantGenerator.
     * Les si_preconditions (SI(x) ≥ 0) sont injectées comme prémisses dans φ3/φ4.
     *
     * @param si_preconditions SI(x) ≥ 0 pour chaque SI (vide si pas de SI)
     */
    virtual std::vector<MotzkinContext> getConstraints(
        const std::vector<LinearInequality>& si_preconditions = {}) const = 0;
    
    /**
     * @brief Retourne les informations sur les paramètres SMT
     */
    virtual TemplateParameters getParameters() const = 0;

    /**
     * @brief Extrait les composantes de la ranking function depuis le modèle SMT
     *
     * Appelé après un check-sat SAT pour construire le TerminationArgument.
     * Chaque template connaît sa propre structure de paramètres :
     * - AffineTemplate    → 1 composante
     * - NestedTemplate    → k composantes (partageant un δ)
     * - LexicographicTemplate → k composantes (avec δ par composante)
     *
     * @param solver       Solveur SMT (modèle disponible après SAT)
     * @param program_vars Variables de programme dans l'ordre d'init()
     * @return Vecteur de RankingFunction, une par composante
     */
    virtual std::vector<RankingFunction> extractRankingFunctions(
        std::shared_ptr<SMTSolver> solver,
        const std::vector<std::string>& program_vars) const = 0;
    
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