#ifndef NONTERMINATION_TECHNIQUE_H
#define NONTERMINATION_TECHNIQUE_H

#include <memory>
#include <string>
#include <map>
#include <iostream>

#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

struct NonTerminationResult {
    enum class Type {
        UNKNOWN,              // Aucune preuve trouvée
        FIXPOINT,             // Point fixe détecté
        GEOMETRIC_UNBOUNDED,  // Exécution géométrique unbounded
        GEOMETRIC_FIXPOINT    // Point fixe via argument géométrique
    };

    Type type;
    bool is_nonterminating;
    std::string description;
    std::string technique_name;

    // État témoin (optionnel selon la technique)
    std::map<std::string, double> witness_state;

    // Temps d'exécution en millisecondes
    double execution_time_ms;

    // Preuve détaillée (trace qui boucle à l'infini)
    std::string proof_details;

    NonTerminationResult()
        : type(Type::UNKNOWN)
        , is_nonterminating(false)
        , description("No nontermination proof found")
        , technique_name("Unknown")
        , execution_time_ms(0.0)
        , proof_details("") {}

    NonTerminationResult(Type t, bool nt, const std::string& desc)
        : type(t)
        , is_nonterminating(nt)
        , description(desc)
        , technique_name("Unknown")
        , execution_time_ms(0.0)
        , proof_details("") {}
};

/**
 * @brief Interface abstraite pour les techniques d'analyse de non-terminaison
 * 
 * Architecture modulaire permettant :
 * - Ajout facile de nouvelles techniques (Fixpoint, Geometric, Recurrence Sets, etc.)
 * - Exécution séquentielle de plusieurs techniques
 * - Séparation claire entre logique métier et orchestration
 * 
 * Pattern similaire à RankingTemplate pour la terminaison
 */
class NonTerminationTechniqueInterface {
public:
    /**
     * @brief Résultat unifié d'une analyse de non-terminaison
     */

    
    virtual ~NonTerminationTechniqueInterface() = default;
    
    // ========================================================================
    // MÉTHODES ABSTRAITES - À implémenter par chaque technique
    // ========================================================================
    
    /**
     * @brief Initialise la technique avec le programme lasso
     * DOIT être appelé avant analyze()
     */
    virtual void init(const LassoProgram& lasso) = 0;
    
    /**
     * @brief Analyse le programme et cherche une preuve de non-terminaison
     * @param solver Le solveur SMT à utiliser
     * @return Résultat de l'analyse
     */
    virtual NonTerminationResult analyze(std::shared_ptr<SMTSolver> solver) = 0;
    
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
    virtual void printResult(const NonTerminationResult& result) const = 0;
    
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

#endif // NONTERMINATION_TECHNIQUE_H