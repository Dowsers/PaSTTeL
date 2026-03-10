#ifndef MOTZKIN_TRANSFORM_H
#define MOTZKIN_TRANSFORM_H

#include <vector>
#include <string>
#include <set>
#include <memory>
#include <unordered_set>
#include <atomic>

#include "linear_inequality.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * Transformation de Motzkin (Motzkin's Transposition Theorem)
 * 
 * THÉORÈME DE MOTZKIN:
 * Un système d'inégalités linéaires est INSATISFIABLE ssi il peut être
 * combiné linéairement (avec des coefficients non-négatifs) pour obtenir
 * une contradiction.
 * 
 * Formellement:
 * ∀x. ¬(A·x + b ≥ 0 ∧ B·x + d > 0)
 * ⟺
 * ∃λ,μ. λ ≥ 0 ∧ μ ≥ 0 ∧ λ·A + μ·B = 0 ∧ λ·b + μ·d ≤ 0 ∧ (λ·b < 0 ∨ μ ≠ 0)
 * 
 * où:
 * - A·x + b ≥ 0 : inégalités non-strictes
 * - B·x + d > 0 : inégalités strictes
 * - λ, μ : coefficients de Motzkin (variables à trouver par SMT)
 * 
 * UTILISATION:
 * Cette classe prend un ensemble d'inégalités linéaires (avec variables de
 * programme ET paramètres de template) et génère les contraintes SMT qui
 * éliminent les variables de programme, ne laissant que les paramètres.
 * 
 * ARCHITECTURE:
 * - Input: vector<LinearInequality> (avec AffineTerm paramétriques)
 * - Output: Contraintes SMT ajoutées au solveur
 * - Les variables de programme (x, x') sont éliminées
 * - Les paramètres (RANKING_C_i, SUP_INVAR_i_j, etc.) restent
 * 
 * Référence: A. Schrijver, "Theory of Linear and Integer Programming", 1986
 */
class MotzkinTransformation {
public:
    MotzkinTransformation();
    
    /**
     * Ajoute les contraintes Motzkin au solveur SMT
     * 
     * Cette méthode:
     * 1. Crée des coefficients de Motzkin λ_i pour chaque inégalité
     * 2. Génère les contraintes de positivité (λ_i ≥ 0)
     * 3. Génère les contraintes d'égalité (Σ λ_i * A_i = 0 pour chaque var)
     * 4. Génère la contrainte de constante (Σ λ_i * b_i ≤ 0)
     * 5. Génère la contrainte de strictness ((Σ λ_i * b_i < 0) ∨ (Σ μ_j > 0))
     * 
     * @param constraints Les inégalités linéaires (déjà avec ¬conclusion)
     * @param program_vars Les variables de programme à éliminer (x, x')
     * @param solver Le solveur SMT où ajouter les contraintes
     * @param annotation Description du contexte (pour debug)
     */
    void addConstraintsToSolver(
        const std::vector<LinearInequality>& constraints,
        const std::unordered_set<std::string>& program_vars,
        std::shared_ptr<SMTSolver> solver,
        const std::string& annotation = "");

private:
    // État interne
    std::vector<LinearInequality> m_inequalities;
    std::unordered_set<std::string> m_program_vars;
    std::vector<std::string> m_motzkin_coefficients;  // λ_0, λ_1, ..., λ_n
    
    // ID unique par instance, obtenu depuis un atomic global (thread-safe)
    int m_instance_id;
    static std::atomic<int> s_instance_counter;

    // ========================================================================
    // GÉNÉRATION DES CONTRAINTES MOTZKIN
    // ========================================================================
    
    /**
     * Enregistre les noms des coefficients de Motzkin (motzkin_i_j)
     */
    void registerMotzkinCoefficients();
    
    /**
     * Génère: λ_i ≥ 0 pour chaque i
     */
    void generatePositivityConstraints(std::shared_ptr<SMTSolver> solver);
    
    /**
     * Génère les contraintes spéciales sur certains λ_i:
     * - ONE: λ_i = 1
     * - ZERO_AND_ONE: λ_i ∈ {0, 1}
     * - ANYTHING: pas de contrainte supplémentaire (juste λ_i ≥ 0)
     */
    void generateMotzkinCoefficientConstraints(std::shared_ptr<SMTSolver> solver);
    
    /**
     * Génère: Σ_i λ_i * coef_i(var) = 0 pour chaque variable de programme
     * 
     * C'est la contrainte clé qui élimine les variables de programme.
     * Pour que le système soit insatisfiable, la combinaison linéaire
     * des inégalités doit donner 0 pour chaque variable.
     */
    void generateEqualityConstraints(std::shared_ptr<SMTSolver> solver);
    
    /**
     * Génère: Σ_i λ_i * b_i ≤ 0
     * 
     * La somme pondérée des constantes doit être non-positive
     */
    void generateConstantConstraint(std::shared_ptr<SMTSolver> solver);
    
    /**
     * Génère: (Σ_i λ_i * b_i < 0) ∨ (Σ_j μ_j > 0)
     * 
     * où i parcourt les inégalités non-strictes et j les strictes.
     * 
     * Cette disjonction assure qu'on a une vraie contradiction:
     * - Soit la combinaison donne une constante strictement négative
     * - Soit au moins une inégalité stricte est utilisée (μ_j > 0)
     */
    void generateStrictConstraint(std::shared_ptr<SMTSolver> solver);
    
    // ========================================================================
    // UTILITAIRES SMT
    // ========================================================================

    /**
     * Convertit un AffineTerm en expression SMT-LIB2
     *
     * Exemple: {RANKING_C_0: 2, constant: 3} → "(+ (* 2 RANKING_C_0) 3)"
     */
    std::string affineTermToSMT(const AffineTerm& term) const;

    /**
     * Multiplie un AffineTerm par un coefficient de Motzkin
     *
     * Exemple: term={RANKING_C_0: 2}, motzkin="λ_0"
     *          → "(* λ_0 (* 2 RANKING_C_0))" simplifié en "(* 2 (* λ_0 RANKING_C_0))"
     */
    std::string multiplyByMotzkin(
        const AffineTerm& term,
        const std::string& motzkin) const;

    // ========================================================================
    // MODE LINÉAIRE
    // ========================================================================

    /**
     * Vérifie si tous les termes affines d'une inégalité sont des constantes
     * numériques pures (pas de paramètres template comme SUP_INVAR, RANKING_C).
     *
     * En mode linéaire : si false → le coefficient Motzkin doit être énuméré
     * dans {0, 1} au lieu d'être une variable libre (pour rester en LRA).
     */
    bool allAffineTermsAreConstant(const LinearInequality& ineq) const;

    /**
     * Génère toutes les formules SMT Motzkin (égalité + constante + strictness)
     * pour un vecteur de coefficients donné, et retourne les formules comme
     * strings (sans les ajouter au solveur).
     *
     * Utilisé par le chemin d'énumération pour construire les branches {0, 1}
     * avant de les envelopper dans (or branch_0 branch_1 ...).
     *
     * Les entrées coef_for[i] == "0.0" sont ignorées (contribution nulle).
     *
     * @param coef_for Coefficient Motzkin à utiliser pour chaque inégalité :
     *                 "0.0", "1.0", ou un nom de variable libre (motzkin_k_i)
     * @return Vecteur de formules SMT-LIB2 à combiner en (and ...)
     */
    std::vector<std::string> generateAllFormulas(
        const std::vector<std::string>& coef_for) const;
};

#endif // MOTZKIN_TRANSFORM_H