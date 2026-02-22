#ifndef SMT_SOLVER_CVC5_H
#define SMT_SOLVER_CVC5_H

#include <cvc5/cvc5.h>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <stack>

#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Implémentation concrète de SMTSolver utilisant CVC5 C++ API
 *
 * Cette classe hérite de SMTSolver et fournit une implémentation complète
 * utilisant directement la bibliothèque CVC5 en C++. Elle supporte :
 * - push/pop pour la gestion de contextes
 * - Déclaration de variables (Int, Real, Bool)
 * - Ajout d'assertions SMT-LIB2
 * - Vérification SAT/UNSAT
 * - Extraction de modèles
 */
class SMTSolverCVC5 : public SMTSolver {
private:
    cvc5::TermManager m_tm;          // Term manager (doit être initialisé avant m_solver)
    cvc5::Solver m_solver;           // Solveur CVC5

    // Cache des variables déclarées (nom -> terme CVC5)
    std::map<std::string, cvc5::Term> m_variables;

    // Cache des fonctions déclarées (nom -> terme CVC5)
    std::map<std::string, cvc5::Term> m_functions;

    // Cache des axiomes (pour propagation lors du clonage)
    std::vector<std::string> m_axioms;

    // Compteur d'assertions
    size_t m_assertion_count;

    // Stack pour gérer les push/pop
    std::stack<size_t> m_assertion_stack;

    // Options
    bool m_verbose;

public:
    /**
     * @brief Constructeur
     * @param verbose Active les messages de debug
     */
    SMTSolverCVC5(bool verbose = false);

    /**
     * @brief Destructeur
     * Thread-safe grâce à un mutex interne pour éviter les problèmes
     * avec le garbage collector de CVC5 en mode parallèle
     */
    virtual ~SMTSolverCVC5();

    // ========================================================================
    // MÉTHODES VIRTUELLES HÉRITÉES DE SMTSolver
    // ========================================================================

    /**
     * @brief Sauvegarde le contexte actuel (push)
     */
    void push() override;

    /**
     * @brief Restaure le contexte précédent (pop)
     */
    void pop() override;

    /**
     * @brief Déclare une nouvelle variable
     * @param name Nom de la variable
     * @param type Type SMT-LIB2 ("Int", "Real", "Bool")
     */
    void declareVariable(const std::string& name, const std::string& type) override;

    /**
     * @brief Déclare une fonction non interprétée
     * @param name Nom de la fonction
     * @param signature Signature SMT-LIB2 (ex: "(Int Int) Bool" ou "Ref Int")
     */
    void declareFunction(const std::string& name, const std::string& signature) override;

    /**
     * @brief Ajoute un axiome (assertion globale avec quantificateurs)
     * @param axiom Formule SMT-LIB2 de l'axiome (sans le assert)
     */
    void addAxiom(const std::string& axiom) override;

    /**
     * @brief Ajoute une assertion au solveur
     * @param assertion Formule SMT-LIB2 sous forme de string
     */
    void addAssertion(const std::string& assertion) override;

    /**
     * @brief Vérifie la satisfiabilité
     * @return true si SAT, false si UNSAT
     */
    bool checkSat() override;

    /**
     * @brief Récupère la valeur d'une variable dans le modèle
     * @param var_name Nom de la variable
     * @return Valeur de la variable (convertie en double)
     */
    double getValue(const std::string& var_name) override;

    /**
     * @brief Compte le nombre d'assertions
     * @return Nombre d'assertions ajoutées
     */
    size_t getAssertionCount() const override;

    /**
     * @brief Vérifie si une variable existe
     */
    bool variableExists(const std::string& name) const override;

    /**
     * @brief Reset complet du solveur
     */
    void reset() override;

    /**
     * @brief Crée une copie indépendante du solver
     * @return Un nouveau SMTSolverCVC5 avec les mêmes paramètres
     */
    std::shared_ptr<SMTSolver> clone() const override;

    // ========================================================================
    // MÉTHODES SPÉCIFIQUES À CVC5
    // ========================================================================

    /**
     * @brief Obtient le solveur CVC5 (pour usage avancé)
     */
    cvc5::Solver& getSolver() { return m_solver; }

    /**
     * @brief Obtient une variable CVC5 par son nom
     * @param name Nom de la variable
     * @return Terme CVC5 correspondant
     * @throws std::runtime_error si la variable n'existe pas
     */
    cvc5::Term getVariable(const std::string& name);

    /**
     * @brief Interrompt immédiatement tout checkSat() en cours dans ce contexte CVC5.
     * Thread-safe : utilise cvc5::Solver::interrupt().
     */
    void interrupt() override;

    /**
     * @brief Affiche les statistiques du solveur
     */
    void printStatistics() const;

private:
    /**
     * @brief Parse une expression SMT-LIB2 et la convertit en terme CVC5
     * @param smt_string Expression SMT-LIB2
     * @return Terme CVC5
     */
    cvc5::Term parseSmtLib2(const std::string& smt_string);

    /**
     * @brief Parse manuel (fallback) pour expressions simples
     * @param smt_string Expression SMT-LIB2
     * @return Terme CVC5
     */
    cvc5::Term parseSmtLib2Manual(const std::string& smt_string);

    /**
     * @brief Convertit un type SMT-LIB2 en sort CVC5
     * @param type Type SMT-LIB2 ("Int", "Real", "Bool")
     * @return Sort CVC5 correspondant
     */
    cvc5::Sort getCVC5Sort(const std::string& type);

    /**
     * @brief Parse récursivement les tokens d'une S-expression
     * @param tokens Liste de tokens
     * @param pos Position courante (modifiée par la fonction)
     * @return Terme CVC5 correspondant
     */
    cvc5::Term parseSexpTokens(const std::vector<std::string>& tokens, size_t& pos);

    /**
     * @brief Parse récursivement avec support des variables liées (quantificateurs)
     * @param tokens Liste de tokens
     * @param pos Position courante (modifiée par la fonction)
     * @param bound_vars Variables liées par des quantificateurs englobants
     * @return Terme CVC5 correspondant
     */
    cvc5::Term parseSexpTokensWithBindings(
        const std::vector<std::string>& tokens,
        size_t& pos,
        const std::map<std::string, cvc5::Term>& bound_vars);

    /**
     * @brief Parse un quantificateur (forall ou exists)
     * @param quantifier "forall" ou "exists"
     * @param tokens Liste de tokens
     * @param pos Position courante (modifiée par la fonction)
     * @param outer_bound_vars Variables liées par des quantificateurs englobants
     * @return Terme CVC5 quantifié
     */
    cvc5::Term parseQuantifier(
        const std::string& quantifier,
        const std::vector<std::string>& tokens,
        size_t& pos,
        const std::map<std::string, cvc5::Term>& outer_bound_vars);

    /**
     * @brief Construit un terme CVC5 à partir d'un opérateur et de ses arguments
     * @param op Opérateur ("+", "-", "and", ">=", etc.)
     * @param args Arguments du terme
     * @return Terme CVC5 résultant
     */
    cvc5::Term buildTerm(const std::string& op, const std::vector<cvc5::Term>& args);

};

#endif // SMT_SOLVER_CVC5_H
