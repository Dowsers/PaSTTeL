#ifndef NONTERMINATION_ANALYZER_H
#define NONTERMINATION_ANALYZER_H

#include <memory>
#include <string>

#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "nontermination/nontermination_technique_interface.h"

/**
 * @brief Analyseur unifié de non-terminaison
 *
 * Orchestre les différentes techniques d'analyse de non-terminaison
 * dans l'ordre suivant :
 * 1. FixpointChecker - Recherche de points fixes simples
 * 2. GeometricNonTerminationSynthesizer - Arguments géométriques
 *
 */
class NonTerminationAnalyzer {
public:

    /**
     * @brief Résultat unifié de l'analyse de non-terminaison
     */

    /**
     * @brief Constructeur
     */
    NonTerminationAnalyzer();
    
    /**
     * @brief Analyse complète de non-terminaison
     *
     * Stratégies :
     * - Mode séquentiel : essaie chaque technique l'une après l'autre
     * - Mode parallèle : lance toutes les techniques en parallèle,
     *                    s'arrête dès que l'une réussit (early stopping)
     *
     * @param lasso Le programme lasso à analyser
     * @param solver Le solveur SMT à utiliser
     * @param parallel True pour mode parallèle, false pour séquentiel (défaut)
     * @return Résultat unifié de l'analyse
     */
    NonTerminationResult analyze(const LassoProgram& lasso,
                                std::shared_ptr<SMTSolver> solver,
                                bool parallel = false);
    
    /**
     * @brief Ajoute une technique à la liste
     */
    void addTechnique(std::unique_ptr<NonTerminationTechniqueInterface> technique);
    
    /**
     * @brief Configure les techniques par défaut (Fixpoint → Geometric)
     */
    void setDefaultTechniques();
    
    /**
     * @brief Efface toutes les techniques
     */
    void clearTechniques();
    
    /**
     * @brief Affiche le résultat
     */
    void printResult(const NonTerminationResult& result) const;

    /**
     * @brief Récupère tous les résultats collectés lors de la dernière analyse
     */
    const std::vector<NonTerminationResult>& getAllResults() const { return all_results_; }

private:
    std::vector<std::unique_ptr<NonTerminationTechniqueInterface>> techniques_;
    std::vector<NonTerminationResult> all_results_;  // Tous les résultats collectés

    /**
     * @brief Exécution séquentielle des techniques
     */
    NonTerminationResult analyzeSequential(const LassoProgram& lasso,
                                          std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Exécution parallèle avec early stopping
     */
    NonTerminationResult analyzeParallel(const LassoProgram& lasso,
                                        std::shared_ptr<SMTSolver> solver);
};

#endif // NONTERMINATION_ANALYZER_H