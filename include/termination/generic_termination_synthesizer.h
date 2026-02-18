#ifndef GENERIC_TERMINATION_SYNTHESIZER_H
#define GENERIC_TERMINATION_SYNTHESIZER_H

#include <memory>
#include <vector>
#include <map>
#include <string>

#include "templates/ranking_template.h"
#include "termination/motzkin_transform.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Synthesizer générique fonctionnant avec n'importe quel RankingTemplate
 *
 *
 * Workflow:
 * 1. Template génère les contraintes (MotzkinContext)
 * 2. Synthesizer applique Motzkin et encode en SMT
 * 3. Résolution SMT
 * 4. Extraction des résultats (RankingFunction + SupportingInvariants)
 */
class GenericTerminationSynthesizer {
public:
    /**
     * @brief Structure pour stocker une fonction de ranking
     */
    struct RankingFunction {
        std::map<std::string, double> coefficients;  // var → coef
        double constant;
        double delta;  // Pour φ3
        
        RankingFunction() : constant(0.0), delta(0.0) {}
        std::string toString(const std::vector<std::string>& vars) const;
    };
    
    /**
     * @brief Structure pour stocker un supporting invariant
     */
    struct SupportingInvariant {
        std::map<std::string, double> coefficients;  // var → coef
        double constant;
        bool is_strict;
        
        SupportingInvariant() : constant(0.0), is_strict(false) {}
        std::string toString(const std::vector<std::string>& vars) const;
    };
    
    /**
     * @brief Résultat de la synthèse
     */
    struct SynthesisResult {
        bool is_valid;                              // True si SAT
        std::map<std::string, double> parameters;   // Valeurs des paramètres (raw)
        std::string template_name;                  // Nom du template utilisé
        std::string description;                    // Description
        
        SynthesisResult() : is_valid(false) {}
    };
    
    /**
     * @brief Constructeur
     * @param lasso Programme lasso
     * @param template_ptr Template à utiliser
     * @param solver Solveur SMT
     */
    GenericTerminationSynthesizer(
        const LassoProgram& lasso,
        RankingTemplate* template_ptr,
        std::shared_ptr<SMTSolver> solver);
    
    /**
     * @brief Lance la synthèse
     * @return Résultat avec paramètres si SAT
     */
    SynthesisResult synthesize();
    
    /**
     * @brief Récupère la ranking function (après synthesize() == SAT)
     */
    const RankingFunction& getRankingFunction() const;
    
    /**
     * @brief Récupère les supporting invariants (après synthesize() == SAT)
     */
    const std::vector<SupportingInvariant>& getSupportingInvariants() const;
    
    /**
     * @brief Récupère le template utilisé
     */
    const RankingTemplate& getTemplate() const { return *template_; }
    
    /**
     * @brief Affiche les résultats
     */
    void printResults(const SynthesisResult& result) const;
    
private:
    // Données
    const LassoProgram& lasso_;
    RankingTemplate* template_;
    std::shared_ptr<SMTSolver> solver_;
    
    // État
    bool synthesized_;
    SynthesisResult last_result_;
    
    // Résultats structurés
    RankingFunction ranking_function_;
    std::vector<SupportingInvariant> supporting_invariants_;
    
    // ========================================================================
    // PIPELINE DE SYNTHÈSE
    // ========================================================================
    
    /**
     * @brief Déclare les paramètres SMT du template
     */
    void declareParameters(const RankingTemplate::TemplateParameters& params);
    
    /**
     * @brief Applique Motzkin à tous les contextes
     */
    void applyMotzkinTransformations(
        const std::vector<RankingTemplate::MotzkinContext>& contexts);
    
    /**
     * @brief Ajoute les contraintes de non-trivialité
     */
    void addNonTrivialityConstraints(
        const RankingTemplate::TemplateParameters& params);
    
    /**
     * @brief Extrait les valeurs des paramètres depuis le modèle SAT
     */
    std::map<std::string, double> extractParameters(
        const RankingTemplate::TemplateParameters& params);
    
    /**
     * @brief Extrait la ranking function et les SI depuis les paramètres
     */
    void extractResults(const RankingTemplate::TemplateParameters& params);

    // ========================================================================
    // NORMALISATION GCD
    // ========================================================================
    
    /**
     * @brief Calcule le GCD de tous les coefficients (incluant constante)
     * 
     * @param coefficients Vecteur des coefficients
     * @param constant Valeur de la constante
     * @return GCD de tous les coefficients (valeur absolue)
     */
    long long computeGCD(
        const std::vector<double>& coefficients,
        double constant) const;
    
    /**
     * @brief Algorithme d'Euclide pour calculer GCD(a, b)
     */
    long long gcd(long long a, long long b) const;
    
    /**
     * @brief Normalise une RankingFunction par son GCD
     */
    void normalizeRankingFunction(RankingFunction& rf, long long gcd_value) const;
    
    /**
     * @brief Normalise un SupportingInvariant par son GCD
     */
    void normalizeSupportingInvariant(SupportingInvariant& si, long long gcd_value) const;
};

#endif // GENERIC_TERMINATION_SYNTHESIZER_H