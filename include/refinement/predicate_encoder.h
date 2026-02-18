#ifndef PREDICATE_ENCODER_H
#define PREDICATE_ENCODER_H

#include <string>
#include <vector>
#include <map>

#include "lasso_program.h"
#include "termination/generic_termination_synthesizer.h"

/**
 * @brief Encodeur de prédicats pour le raffinement Refine_ω
 *
 * Prend une RankingFunction et des SupportingInvariants synthétisés,
 * et génère les formules SMT-LIB2 nécessaires pour construire
 * l'automate d'interpolants.
 *
 * Notation :
 * - f(x) : fonction de ranking
 * - SI(x) : supporting invariant (conjonction d'invariants linéaires)
 * - x, y, z : variables du programme
 * - x', y', z' : variables de sortie (lasso.loop.out_vars)
 *
 * Référence :
 * - "Termination Analysis by Learning Terminating Programs" (Heizmann et al.)
 * - "Fairness Modulo Theory: A New Approach to LTL Software Model Checking"
 * - BinaryStatePredicateManager.java dans Ultimate Automizer
 */
class PredicateEncoder {
public:
    /**
     * @brief Ensemble des prédicats encodés en SMT-LIB2
     * 
     * Ces prédicats sont utilisés pour construire l'automate d'interpolants
     * lors du raffinement Refine_ω.
     */
    struct Predicates {
        std::string stem_precondition;       // Contraintes initiales du stem
        std::string stem_postcondition;      // SI(x) après le stem
        std::string honda_predicate;         // Point d'entrée de la boucle
        std::string rank_decrease_and_bound; // f(x') < f(x) ∧ f(x) ≥ 0
        std::string rank_equality;           // f(x') = f(x)
        std::string si_conjunction;          // ⋀ SI_i(x)
        std::string rank_eq_and_si;          // f(x') = f(x) ∧ SI(x')
    };

    PredicateEncoder();

    /**
     * @brief Encode tous les prédicats depuis RankingFunction et SI
     * 
     * Version simplifiée travaillant directement avec les structures
     * de GenericTerminationSynthesizer.
     * 
     * @param rf Fonction de ranking synthétisée
     * @param sis Supporting invariants synthétisés
     * @param lasso Le LassoProgram (pour mapping in_vars → out_vars)
     * @return Structure contenant toutes les formules SMT encodées
     */
    Predicates encode(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
        const LassoProgram& lasso
    );

    /**
     * @brief Affiche les prédicats de manière formatée (debug)
     */
    void printPredicates(const Predicates& preds) const;

private:
    // ============================================================================
    // CONSTRUCTION DES FONCTIONS
    // ============================================================================

    /**
     * @brief Génère la formule SMT pour f(x) ou f(x')
     * 
     * @param rf Fonction de ranking
     * @param vars Variables du programme (in_vars)
     * @param primed Si true, utilise out_vars au lieu de in_vars
     * @param lasso Programme lasso (pour mapping in→out)
     * @return Formule SMT "(+ const (* coef var) ...)"
     */
    std::string buildRankingFunction(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const std::vector<std::string>& vars,
        bool primed,
        const LassoProgram& lasso
    ) const;

    /**
     * @brief Génère la formule SMT pour SI(x) ou SI(x')
     * 
     * @param si Supporting invariant
     * @param vars Variables du programme
     * @param primed Si true, utilise out_vars
     * @param lasso Programme lasso
     * @return Formule SMT avec inégalité "(>= ... 0)" ou "(> ... 0)"
     */
    std::string buildSupportingInvariant(
        const GenericTerminationSynthesizer::SupportingInvariant& si,
        const std::vector<std::string>& vars,
        bool primed,
        const LassoProgram& lasso
    ) const;

    // ============================================================================
    // ENCODAGE DES PRÉDICATS PRINCIPAUX
    // ============================================================================

    /**
     * @brief Génère stem_precondition à partir des contraintes du stem
     * 
     * Encode les contraintes initiales du stem sous forme de conjonction.
     * Si le stem n'a pas de contraintes, retourne "true".
     */
    std::string encodeStemPrecondition(const LassoProgram& lasso) const;

    /**
     * @brief Génère stem_postcondition
     * Postcondition : ⋀ SI_i(x) pour tous les SI
     */
    std::string encodeStemPostcondition(
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
        const LassoProgram& lasso
    ) const;

    /**
     * @brief Génère honda_predicate
     * Honda : Point d'entrée de la boucle
     * Honda = SI(x) ∧ rank_decrease_and_bound
     */
    std::string encodeHondaPredicate(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
        const LassoProgram& lasso
    ) const;

    /**
     * @brief Génère rank_decrease_and_bound
     * f(x') < f(x) ∧ f(x) ≥ 0
     */
    std::string encodeRankDecreaseAndBound(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const LassoProgram& lasso
    ) const;

    /**
     * @brief Génère rank_equality
     * f(x') = f(x)
     */
    std::string encodeRankEquality(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const LassoProgram& lasso
    ) const;

    /**
     * @brief Génère si_conjunction
     * ⋀ SI_i(x) ou ⋀ SI_i(x') selon primed
     */
    std::string encodeSIConjunction(
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
        const LassoProgram& lasso,
        bool primed
    ) const;

    /**
     * @brief Génère rank_eq_and_si
     * f(x') = f(x) ∧ SI(x')
     */
    std::string encodeRankEqAndSI(
        const GenericTerminationSynthesizer::RankingFunction& rf,
        const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
        const LassoProgram& lasso
    ) const;

    // ============================================================================
    // UTILITAIRES POUR L'ENCODAGE DES CONTRAINTES DU STEM
    // ============================================================================

    /**
     * @brief Encode une inégalité linéaire en SMT-LIB2
     * 
     * Convertit une LinearInequality en formule SMT-LIB2.
     * Exemple: 2*x + 3*y - 5 >= 0 devient "(>= (+ (* 2 x) (* 3 y) -5) 0)"
     */
    std::string encodeInequality(const LinearInequality& ineq) const;

    /**
     * @brief Encode un polyèdre (conjonction d'inégalités)
     */
    std::string encodePolyhedron(const std::vector<LinearInequality>& polyhedron) const;

    /**
     * @brief Encode une transition (disjonction de polyèdres)
     */
    std::string encodeTransition(const LinearTransition& transition) const;

    // ============================================================================
    // UTILITAIRES GÉNÉRAUX
    // ============================================================================

    /**
     * @brief Vérifie si un coefficient est proche de zéro
     */
    bool isZero(double value) const;

    /**
     * @brief Joint plusieurs chaînes avec un séparateur
     */
    std::string join(const std::vector<std::string>& strings, const std::string& separator) const;
};

#endif // PREDICATE_ENCODER_H