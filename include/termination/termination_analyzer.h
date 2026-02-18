#ifndef TERMINATION_ANALYZER_H
#define TERMINATION_ANALYZER_H

#include <memory>
#include <vector>
#include <string>
#include <future>
#include <atomic>
#include <mutex>

#include "smtsolvers/SMTSolverInterface.h"
#include "termination/termination_technique_interface.h"

/**
 * @brief Analyseur unifié de terminaison
 *
 * Orchestre les différentes techniques d'analyse de terminaison :
 * - RankingBasedTechnique (Affine, Nested, etc.)
 * - ModelCheckingTechnique (futur)
 * - AbstractInterpretationTechnique (futur)
 * - SizeChangeTechnique (futur)
 *
 * Supporte :
 * - Exécution séquentielle (mode par défaut)
 * - Exécution parallèle avec early stopping (si parallel=true)
 *
 * Pattern symétrique à NonTerminationAnalyzer
 */
class TerminationAnalyzer {
public:
    /**
     * @brief Constructeur (avec techniques par défaut)
     */
    TerminationAnalyzer();

    /**
     * @brief Analyse complète de terminaison
     *
     * Stratégies :
     * - Mode séquentiel : essaie chaque technique l'une après l'autre
     * - Mode parallèle : lance toutes les techniques en parallèle,
     *                    s'arrête dès que l'une réussit (early stopping)
     *
     * @param lasso Le programme lasso à analyser
     * @param solver Le solveur SMT à utiliser
     * @param parallel True pour mode parallèle, false pour séquentiel
     * @return Résultat unifié de l'analyse
     */
    TerminationResult analyze(const LassoProgram& lasso,
                              std::shared_ptr<SMTSolver> solver,
                              bool parallel = false);

    /**
     * @brief Ajoute une technique à la liste
     */
    void addTechnique(std::unique_ptr<TerminationTechniqueInterface> technique);

    /**
     * @brief Configure les techniques par défaut (RankingBased)
     */
    void setDefaultTechniques();

    /**
     * @brief Efface toutes les techniques
     */
    void clearTechniques();

    /**
     * @brief Affiche le résultat
     */
    void printResult(const TerminationResult& result) const;

    /**
     * @brief Récupère tous les résultats collectés lors de la dernière analyse
     */
    const std::vector<TerminationResult>& getAllResults() const { return all_results_; }

private:
    std::vector<std::unique_ptr<TerminationTechniqueInterface>> techniques_;
    std::vector<TerminationResult> all_results_;  // Tous les résultats collectés

    /**
     * @brief Exécution séquentielle des techniques
     */
    TerminationResult analyzeSequential(const LassoProgram& lasso,
                                        std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Exécution parallèle avec early stopping
     */
    TerminationResult analyzeParallel(const LassoProgram& lasso,
                                      std::shared_ptr<SMTSolver> solver);
};

#endif // TERMINATION_ANALYZER_H
