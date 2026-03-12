#ifndef GEOMETRIC_TECHNIQUE_H
#define GEOMETRIC_TECHNIQUE_H

#include <string>
#include <vector>

#include "lasso_program.h"
#include "linear_inequality.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "analysis_technique_interface.h"

/**
 * @brief Paramètres pour l'analyse géométrique de non-terminaison
 *
 */
struct GeometricNonTerminationSettings {
    // Type d'analyse des eigenvalues 
    // LINEAR : lambda fixe = 1 → contraintes purement lineaires (LIA/LRA)
    // NONLINEAR : lambda variable libre → NIA/NRA
    enum class AnalysisType { LINEAR, NONLINEAR };

    int num_gevs = 3;               // Nombre de vecteurs propres généralisés
    bool allow_bounded = true;      // Autoriser λ ≥ 0 (true) ou forcer λ ≥ 1 (false)
    bool nilpotent_components = true; // Autoriser les composantes nilpotentes νᵢ >= 0
    AnalysisType analysis_type = AnalysisType::LINEAR;
};

/**
 * @brief Synthétise des arguments géométriques de non-terminaison (GNTA)
 *
 * Un GNTA est une preuve de non-terminaison basée sur la construction d'une
 * exécution infinie: x + Y·(Σᵢ Jⁱ)·1
 * où Y = matrice des GEVs, J = forme de Jordan (eigenvalues + nilpotent)
 *
 * Stratégie en 2 phases
 * - Phase 1: GEV=0 (recherche de point fixe simple géré par FixpointTechnique)
 * - Phase 2: GEVs complets avec composantes nilpotentes
 *
 * Contraintes encodées:
 * 1. Stem(x₀, x₁)                          [initiation]
 * 2. Loop(x₁, x₁ + y₁ + ... + yₙ)          [première itération]  [point A(x₁, x₁ + Σᵢ y)]
 * 3. Loop(yᵢ, λᵢ·yᵢ + νᵢ·yᵢ₊₁) pour chaque i [comportement rayon]
 * 4. Bornes sur λᵢ et νᵢ                   [domain]
 *
 * Basé sur:
 * - "Proving Non-termination" (Gupta et al., POPL 2008)
 * - "Geometric Nontermination Arguments" (Leike & Heizmann, TACAS 2018)
 */
class GeometricTechnique : public AnalysisTechniqueInterface {
public:
    /**
     * @brief Constructeur avec paramètres par défaut
     * @param settings Paramètres de l'analyse géométrique
     */
    GeometricTechnique(const GeometricNonTerminationSettings& settings = GeometricNonTerminationSettings());

    void init(const LassoProgram& lasso) override;

    /**
     * @brief Synthétise un GNTA pour le programme lasso donné
     *
     * GEVs > 0 → recherche géométrique avec nilpotent
     *
     * @param solver Le solveur SMT à utiliser
     * @return GNTA trouvé, ou UNKNOWN si aucun
     */
    AnalysisResult analyze(std::shared_ptr<SMTSolver> solver) override;
    ProofCertificate getProof() const override { return proof_; }

    std::string getName() const override {
        std::string mode = (settings_.analysis_type == GeometricNonTerminationSettings::AnalysisType::LINEAR)
            ? "L" : "NL";
        return "Geometric" + mode + "(" + std::to_string(settings_.num_gevs) + ")";
    }

    std::string getDescription() const {
        return "Geometric nontermination with " +
            std::to_string(settings_.num_gevs) + " GEV(s), nilpotent=" +
            (settings_.nilpotent_components ? "on" : "off") + ", bounded=" +
            (settings_.allow_bounded ? "on" : "off");
    }

    bool validateConfiguration() const override;

    // Accesseurs
    void setSettings(const GeometricNonTerminationSettings& settings);
    const GeometricNonTerminationSettings& getSettings() const;
    int getNumGEVs() const;

private:
    GeometricNonTerminationSettings settings_;
    const LassoProgram* lasso_;
    bool initialized_;
    ProofCertificate proof_;

    // Résultats extraits
    std::map<std::string, double> state_init;                // État initial x₀
    std::map<std::string, double> state_honda;               // État honda x₁
    std::vector<std::map<std::string, double>> eigenvectors; // Vecteurs propres (GEVs) y₁..yₙ
    std::vector<double> lambdas;                             // Valeurs propres λ₁..λₙ
    std::vector<double> nus;                                 // Composantes nilpotentes ν₁..νₙ₋₁

    /**
     * @brief Encode les contraintes pour un GNTA avec n GEVs
     *
     * Contraintes:
     * 1. Stem(x₀, x₁) si stem présent
     * 2. Loop(x₁, x₁ + y₁ + ... + yₙ) - première itération
     * 3. Loop(yᵢ, λᵢ·yᵢ + νᵢ·yᵢ₊₁) - rayon pour chaque GEV i
     * 4. Bornes: λᵢ ≥ 0 (ou λᵢ ≥ 1), νᵢ ∈ {0,1}
     *
     * @param solver Le solveur SMT
     * @param effective_num_gevs Nombre de GEVs à utiliser (> 0)
     */
    bool encodeConstraints(std::shared_ptr<SMTSolver> solver, int effective_num_gevs);

    /**
     * @brief Déclare les variables SMT: x₀, x₁, yᵢ, λᵢ, νᵢ
     */
    void declareVariables(std::shared_ptr<SMTSolver> solver, int effective_num_gevs);

    /**
     * @brief Ajoute les contraintes du stem: Stem(x₀, x₁)
     */
    void addStemConstraints(std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Version branch-consistante de (première itération + rays).
     *
     * Pour chaque polyèdre p, on combine en un seul (and fi_p ray0_p ray1_p ...),
     * puis on émet le (or ...) externe.
     *
     * Garantit que la première itération et tous les rays utilisent le MÊME polyèdre,
     * éliminant les preuves spurieuses dues au mélange de branches DNF.
     */
    void addCombinedLoopConstraints(std::shared_ptr<SMTSolver> solver, int effective_num_gevs);

    /**
     * @brief Construit les contraintes de première itération pour UN polyèdre.
     * @return Vecteur de chaînes SMT-LIB (une par inégalité satisfaite)
     */
    std::vector<std::string> buildFirstIterConstraintsForPoly(
        const std::vector<LinearInequality>& poly,
        int effective_num_gevs,
        std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Construit les contraintes rayon pour UN polyèdre et UN GEV.
     * @return Vecteur de branches (une par nu_val) — chaque branche est un vecteur de contraintes
     */
    std::vector<std::vector<std::string>> buildRayConstraintsForPoly(
        const std::vector<LinearInequality>& poly,
        int gev_idx,
        int effective_num_gevs,
        std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Ajoute les contraintes d'identité pour variables inchangées (in_ssa == out_ssa)
     * Pour chaque variable identité x: sum(v_i_x) = 0 et v_i_x = λᵢ·v_i_x + νᵢ·v_{i+1}_x
     */
    void addIdentityVariableConstraints(std::shared_ptr<SMTSolver> solver, int effective_num_gevs);

    /**
     * @brief Ajoute les contraintes sur λᵢ et νᵢ
     * - Si allow_bounded: λᵢ ≥ 0
     * - Sinon: λᵢ ≥ 1 et (y₁ ≠ 0 ∨ ... ∨ yₙ ≠ 0)
     * - Si nilpotent_components: νᵢ ∈ {0, 1}
     * - Sinon: νᵢ = 0
     */
    void addEigenvalueAndNilpotentConstraints(std::shared_ptr<SMTSolver> solver, int effective_num_gevs);

    /**
     * @brief Extrait le GNTA depuis le modèle SAT
     */
    ProofCertificate extractGNTA(std::shared_ptr<SMTSolver> solver, int effective_num_gevs);

    /**
     * @brief Vérifie si c'est un fixpoint (tous les GEVs=0 ou tous les λ=0)
     */
    bool isFixpoint() const;
};

#endif // GEOMETRIC_TECHNIQUE_H