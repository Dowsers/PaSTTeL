#ifndef FIXPOINT_TECHNIQUE_H
#define FIXPOINT_TECHNIQUE_H

#include <memory>
#include <map>
#include <string>
#include <optional>

#include "smtsolvers/SMTSolverInterface.h"
#include "lasso_program.h"
#include "analysis_technique_interface.h"

/**
 * @brief Vérifie l'existence de points fixes pour prouver la non-terminaison
 *
 * Un point fixe est un état x où Loop(x, x') avec x = x'.
 * Si un tel point existe et est atteignable depuis le stem,
 * alors le programme ne termine pas.
 *
 * Basé sur "Proving Non-termination" (Gupta et al., POPL 2008)
 */
class FixpointTechnique : public AnalysisTechniqueInterface {
public:

    /**
     * @brief Constructeur
     */
    FixpointTechnique();

    void init(const LassoProgram& lasso) override;

    /**
     * @brief Vérifie l'existence d'un point fixe et retourne un AnalysisResult
     *
     * Encode les contraintes SMT :
     * - Si stem existe : Stem(x₀, x) ∧ Loop(x, x') ∧ (x = x')
     * - Sinon : Loop(x, x') ∧ (x = x')
     *
     * @param solver Le solveur SMT à utiliser
     * @return Résultat contenant le point fixe si trouvé
     */
    AnalysisResult analyze(std::shared_ptr<SMTSolver> solver) override;
    ProofCertificate getProof() const override { return proof_; }

    std::string getName() const override {
        return "Fixpoint";
    }

    bool validateConfiguration() const override;

private:

    const LassoProgram* lasso_;
    bool initialized_;
    ProofCertificate proof_;

    /**
     * @brief Ajoute les contraintes du stem au solveur
     */
    void addStemConstraints(std::shared_ptr<SMTSolver> solver);
    
    /**
     * @brief Ajoute les contraintes de la boucle au solveur
     */
    void addLoopConstraints(std::shared_ptr<SMTSolver> solver);
    
    /**
     * @brief Ajoute les contraintes x = x' (point fixe)
     */
    void addFixpointConstraints(std::shared_ptr<SMTSolver> solver);
    
    /**
     * @brief Extrait les valeurs du point fixe depuis le modèle SAT
     */
    std::map<std::string, double> extractFixpoint(
        std::shared_ptr<SMTSolver> solver);
};

#endif // FIXPOINT_TECHNIQUE_H