#ifndef SMTSOLVER_INTERFACE_H
#define SMTSOLVER_INTERFACE_H


#include <string>
#include <cmath>
#include <cstdint>
#include <utility>

#include "ModelExtractionUtils.h"

// Interface pour solveur SMT
class SMTSolverInterface {
public:
    virtual ~SMTSolverInterface() = default;

    virtual void push() = 0;
    virtual void pop() = 0;
    virtual void addAssertion(const std::string& assertion) = 0;
    virtual bool checkSat() = 0;
    virtual double getValue(const std::string& var) = 0;

    /**
     * @brief Retourne la valeur d'une variable comme rationnel exact (num, den)
     * den est toujours > 0. Par défaut utilise getValue() converti.
     */
    virtual Rational getRationalValue2(const std::string& var) = 0;

    virtual void declareVariable(const std::string& name, const std::string& sort) = 0;
    virtual void declareFunction(const std::string& name, const std::string& signature) = 0;

    /**
     * @brief Ajoute un axiome (assertion globale avec quantificateurs)
     * Les axiomes sont des formules universellement quantifiées (forall, exists)
     * qui définissent des propriétés des fonctions non interprétées.
     * @param axiom La formule SMT-LIB2 de l'axiome (sans le assert)
     */
    virtual void addAxiom(const std::string& axiom) = 0;

    virtual size_t getAssertionCount() const = 0;
    virtual void reset() = 0;
    virtual bool variableExists(const std::string& name) const = 0;

    /**
     * @brief Crée une copie indépendante du solver
     * Utilisé pour la parallélisation (chaque thread a son propre solver)
     * @return Un nouveau solver du même type avec les mêmes paramètres
     */
    virtual std::shared_ptr<SMTSolverInterface> clone() const = 0;

    /**
     * @brief Interrompt immédiatement toute résolution SMT en cours
     * Thread-safe : peut être appelé depuis un autre thread.
     * Utilisé pour annuler un checkSat() bloquant quand une solution
     * a été trouvée par un autre thread.
     */
    virtual void interrupt() {}
};

#endif