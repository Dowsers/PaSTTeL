#ifndef TERMINATION_TECHNIQUE_INTERFACE_H
#define TERMINATION_TECHNIQUE_INTERFACE_H

#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <atomic>
#include <iostream>

#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Résultat unifié d'une analyse de terminaison
 */
struct TerminationResult {
    enum class Type {
        UNKNOWN,                    // Aucune preuve trouvée
        RANKING_BASED              // Preuve par ranking function
    };

    Type type;
    bool is_terminating;
    std::string description;
    std::string technique_name;

    // Témoin (optionnel selon la technique)
    std::map<std::string, int64_t> witness;

    // Temps d'exécution en millisecondes
    double execution_time_ms;

    // Preuve détaillée (ranking function + SI pour ranking-based)
    std::string proof_details;

    TerminationResult()
        : type(Type::UNKNOWN)
        , is_terminating(false)
        , description("No termination proof found")
        , technique_name("Unknown")
        , execution_time_ms(0.0)
        , proof_details("") {}

    TerminationResult(Type t, bool term, const std::string& desc, const std::string& name)
        : type(t)
        , is_terminating(term)
        , description(desc)
        , technique_name(name)
        , execution_time_ms(0.0)
        , proof_details("") {}
};

/**
 * @brief Interface abstraite pour les techniques d'analyse de terminaison
 *
 * Architecture modulaire permettant :
 * - Ajout facile de nouvelles techniques (Ranking, Model Checking, Abstract Interpretation, etc.)
 * - Exécution parallèle de plusieurs techniques avec early stopping
 * - Séparation claire entre logique métier et orchestration
 *
 * Chaque technique implémente cette interface et est gérée par TerminationAnalyzer.
 *
 */
class TerminationTechniqueInterface {
public:
    virtual ~TerminationTechniqueInterface() = default;

    // ========================================================================
    // MÉTHODES ABSTRAITES - À implémenter par chaque technique
    // ========================================================================

    /**
     * @brief Initialise la technique avec le programme lasso
     * DOIT être appelé avant analyze()
     */
    virtual void init(const LassoProgram& lasso) = 0;

    /**
     * @brief Analyse le programme et cherche une preuve de terminaison
     * @param solver Le solveur SMT à utiliser
     * @return Résultat de l'analyse
     */
    virtual TerminationResult analyze(std::shared_ptr<SMTSolver> solver) = 0;

    /**
     * @brief Retourne le nom de la technique (pour logging)
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Retourne une description de la technique
     */
    virtual std::string getDescription() const = 0;

    /**
     * @brief Affiche le résultat de manière formatée
     */
    virtual void printResult(const TerminationResult& result) const = 0;

    // ========================================================================
    // MÉTHODES OPTIONNELLES - Peuvent être override si nécessaire
    // ========================================================================

    /**
     * @brief Valide la configuration de la technique
     * @return true si la configuration est valide
     */
    virtual bool validateConfiguration() const { return true; }

    /**
     * @brief Affiche les informations de la technique
     */
    virtual void printInfo() const {
        std::cout << "Technique: " << getName() << "\n"
                  << "Description: " << getDescription() << std::endl;
    }

    /**
     * @brief Indique si la technique peut être annulée en cours d'exécution
     */
    virtual bool canBeCancelled() const { return true; }

    /**
     * @brief Demande l'annulation de la technique (pour parallélisation)
     * Appelé quand une autre technique a trouvé une preuve
     */
    virtual void cancel() {}
};

#endif // TERMINATION_TECHNIQUE_INTERFACE_H
