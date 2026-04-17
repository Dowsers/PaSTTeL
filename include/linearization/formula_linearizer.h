#ifndef FORMULA_LINEARIZER_H
#define FORMULA_LINEARIZER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

#include "linearization/non_linear_term_handler.h"
#include "parser/sexpr_utils.h"
#include "lasso_program.h"

/**
 * Abstraction d'un terme non-linéaire.
 * Le terme original est remplacé par une variable fraîche,
 * avec une assertion d'égalité pour le solveur SMT.
 *
 * Exemple :
 *   (keccak256 v_x_1)  ->  uf__keccak256__0
 *   assertion : (= uf__keccak256__0 (keccak256 v_x_1))
 */

/**
 * Resultat de la linearisation d'une formule.
 */
struct LinearizationResult {
    std::string linearized_formula;  // Formule avec les termes non-lineaires remplaces
    bool was_modified;               // true si au moins une substitution a ete faite
};

/**
 * FormulaLinearizer - Orchestrateur de linearisation de formules SMT-LIB2.
 *
 * Parcourt recursivement les S-expressions et delegue la detection
 * des termes non-lineaires a des handlers specialises (NonLinearTermHandler).
 *
 * Chaque handler gere un type de terme :
 *   - UFHandler       : fonctions non-interpretees (keccak256, sum, ...)
 *   - ArrayHandler    : operations sur arrays (select, store) [futur]
 *   - StringHandler   : operations sur strings (str.len, ...) [futur]
 *
 * Les algorithmes qui n'ont pas besoin de linearite (fixpoint, geometric)
 * peuvent utiliser les formules originales en n'ajoutant aucun handler.
 *
 * Usage :
 *   FormulaLinearizer linearizer;
 *   linearizer.addHandler(std::make_unique<UFHandler>(function_names));
 *   LinearizationResult result = linearizer.linearize(formula);
 */
class FormulaLinearizer {
public:
    FormulaLinearizer();

    /**
     * Ajoute un handler de termes non-lineaires.
     * Les handlers sont interroges dans l'ordre d'ajout.
     */
    void addHandler(std::unique_ptr<NonLinearTermHandler> handler);

    /**
     * Linearise une formule SMT-LIB2.
     * Parcourt recursivement la S-expression et remplace chaque terme
     * reconnu par un handler par une variable fraiche.
     * Si un meme terme apparait plusieurs fois, la meme variable est reutilisee.
     */
    LinearizationResult linearize(const std::string& formula);

    void storeAbstractionsToLasso(LassoProgram& lasso);

    /**
     * Retourne toutes les abstractions creees (cumulatives sur tous les appels).
     */
    const std::vector<FunctionAbstraction>& getAbstractions() const;

    /**
     * Retourne le mapping variable fraiche -> terme original.
     */
    const std::map<std::string, std::string>& getAbstractionMap() const;

    /**
     * Reinitialise les abstractions (pas les handlers).
     */
    void reset();

    /**
     * Retourne true si au moins un handler est enregistre.
     */
    bool hasHandlers() const;

private:
    // Handlers enregistres (interroges dans l'ordre)
    std::vector<std::unique_ptr<NonLinearTermHandler>> m_handlers;

    // Abstractions creees (accumulees sur plusieurs appels a linearize())
    std::vector<FunctionAbstraction> m_abstractions;

    // Mapping : terme original -> variable fraiche (pour reutiliser la meme variable)
    std::map<std::string, std::string> m_call_to_var;

    // Mapping inverse : variable fraiche -> terme original
    std::map<std::string, std::string> m_var_to_call;

    // Compteur pour generer des noms uniques
    int m_counter;

    /**
     * Parcourt recursivement une S-expression et remplace les termes non-lineaires.
     */
    std::string linearizeExpr(const std::string& expr);

    /**
     * Cree ou recupere la variable fraiche pour un terme donne.
     * @param handler Le handler qui a reconnu le terme
     */
    std::string getOrCreateFreshVar(const std::string& full_call,
                                    const std::string& op,
                                    const NonLinearTermHandler& handler,
                                    const std::vector<std::string>& args);

    /**
     * Trouve le handler qui gere cet operateur, ou nullptr.
     */
    NonLinearTermHandler* findHandler(const std::string& op) const;

    /**
     * Decoupe une S-expression en sous-expressions de premier niveau.
     * Delegates to SExprUtils::splitSExpr().
     */
    static std::vector<std::string> splitSExpr(const std::string& expr) {
        return SExprUtils::splitSExpr(expr);
    }

    /**
     * Verifie si un token est un operateur arithmetique/logique builtin SMT-LIB.
     */
    static bool isBuiltinOperator(const std::string& op);
};

#endif // FORMULA_LINEARIZER_H
