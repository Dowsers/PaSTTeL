#ifndef SMT_SOLVER_Z3_H
#define SMT_SOLVER_Z3_H

#include <z3++.h>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <stack>

#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Implémentation concrète de SMTSolver utilisant Z3 C++ API
 * 
 * Cette classe hérite de SMTSolver et fournit une implémentation complète
 * utilisant directement la bibliothèque Z3 en C++. Elle supporte :
 * - push/pop pour la gestion de contextes
 * - Déclaration de variables (Int, Real, Bool)
 * - Ajout d'assertions SMT-LIB2
 * - Vérification SAT/UNSAT
 * - Extraction de modèles
 */
class SMTSolverZ3 : public SMTSolver {
private:
    z3::context m_context;           // Contexte Z3
    z3::solver m_solver;             // Solveur Z3
    
    // Cache des variables déclarées (nom -> expression Z3)
    std::map<std::string, z3::expr> m_variables;

    // Cache des fonctions déclarées (nom -> func_decl Z3)
    std::map<std::string, z3::func_decl> m_functions;

    // Cache des axiomes (pour propagation lors du clonage)
    std::vector<std::string> m_axioms;

    // Compteur d'assertions
    size_t m_assertion_count;
    
    // Stack pour gérer les push/pop
    std::stack<size_t> m_assertion_stack;
    
    // Options
    bool m_verbose;

    // Set to true by interrupt() to silently ignore subsequent Z3 operations
    bool m_interrupted;
    
public:
    /**
     * @brief Constructeur
     * @param verbose Active les messages de debug
     */
    SMTSolverZ3(bool verbose = false);
    
    /**
     * @brief Destructeur
     */
    virtual ~SMTSolverZ3() = default;
    
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
     * @brief Retourne la valeur exacte comme rationnel (numérateur, dénominateur)
     * Utilise directement l'API Z3 pour éviter la perte de précision.
     */
    std::pair<int64_t, int64_t> getRationalValue(const std::string& var_name) override;
    
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
     * @return Un nouveau SMTSolverZ3 avec les mêmes paramètres
     */
    std::shared_ptr<SMTSolver> clone() const override;

    // ========================================================================
    // MÉTHODES SPÉCIFIQUES À Z3
    // ========================================================================
    
    /**
     * @brief Obtient le contexte Z3 (pour usage avancé)
     */
    z3::context& getContext() { return m_context; }
    
    /**
     * @brief Obtient le solveur Z3 (pour usage avancé)
     */
    z3::solver& getSolver() { return m_solver; }
    
    /**
     * @brief Obtient une variable Z3 par son nom
     * @param name Nom de la variable
     * @return Expression Z3 correspondante
     * @throws std::runtime_error si la variable n'existe pas
     */
    z3::expr getVariable(const std::string& name);
    
    /**
     * @brief Interrompt immédiatement tout checkSat() en cours dans ce contexte Z3.
     * Thread-safe : utilise z3::context::interrupt().
     */
    void interrupt() override;

    /**
     * @brief Affiche les statistiques du solveur
     */
    void printStatistics() const;
    
    /**
     * @brief Affiche le modèle (si SAT)
     */
    void printModel() const;
    
private:
    /**
     * @brief Parse une expression SMT-LIB2 et la convertit en expr Z3
     *        (recursive S-expression parser, no string re-serialization)
     */
    z3::expr parseSmtLib2(const std::string& smt_string);

    /** @brief Legacy fallback (redirects to parseSmtLib2) */
    z3::expr parseSmtLib2Manual(const std::string& smt_string);

    /** @brief Recursive S-expression token parser */
    z3::expr parseSexpTokens(
        const std::vector<std::string>& tokens, size_t& pos);

    /** @brief Parser with support for bound variables (quantifiers, let) */
    z3::expr parseSexpTokensWithBindings(
        const std::vector<std::string>& tokens,
        size_t& pos,
        const std::map<std::string, z3::expr>& bound_vars);

    /** @brief Parse a quantifier (forall/exists) */
    z3::expr parseQuantifier(
        const std::string& quantifier,
        const std::vector<std::string>& tokens,
        size_t& pos,
        const std::map<std::string, z3::expr>& outer_bound_vars);

    /** @brief Parse a let expression */
    z3::expr parseLet(
        const std::vector<std::string>& tokens,
        size_t& pos,
        const std::map<std::string, z3::expr>& outer_bound_vars);

    /** @brief Build a Z3 expr from operator name and arguments */
    z3::expr buildTerm(
        const std::string& op, const std::vector<z3::expr>& args);
    
    /**
     * @brief Convertit un type SMT-LIB2 en sort Z3
     * @param type Type SMT-LIB2 ("Int", "Real", "Bool")
     * @return Sort Z3 correspondant
     */
    z3::sort getZ3Sort(const std::string& type);
    
};

#endif // SMT_SOLVER_Z3_H