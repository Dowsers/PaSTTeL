#ifndef SMTSOLVER_INTERFACE_H
#define SMTSOLVER_INTERFACE_H


#include <string>
#include <cmath>
#include <cstdint>
#include <utility>


// Interface pour solveur SMT
class SMTSolver {
public:
    virtual ~SMTSolver() = default;

    virtual void push() = 0;
    virtual void pop() = 0;
    virtual void addAssertion(const std::string& assertion) = 0;
    virtual bool checkSat() = 0;
    virtual double getValue(const std::string& var) = 0;

    /**
     * @brief Retourne la valeur d'une variable comme rationnel exact (num, den)
     * den est toujours > 0. Par défaut utilise getValue() converti.
     */
    virtual std::pair<int64_t, int64_t> getRationalValue(const std::string& var) {
        double v = getValue(var);
        // Approximation par défaut : conversion double -> rationnel
        // Les sous-classes peuvent surcharger pour plus de précision
        int64_t den = 1;
        while (std::abs(v * den - std::round(v * den)) > 1e-9 && den < 1000000) den *= 2;
        return { static_cast<int64_t>(std::round(v * den)), den };
    }
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
    virtual std::shared_ptr<SMTSolver> clone() const = 0;

    /**
     * @brief Interrompt immédiatement toute résolution SMT en cours
     * Thread-safe : peut être appelé depuis un autre thread.
     * Utilisé pour annuler un checkSat() bloquant quand une solution
     * a été trouvée par un autre thread.
     */
    virtual void interrupt() {}
};

#endif