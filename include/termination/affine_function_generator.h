#ifndef AFFINE_FUNCTION_GENERATOR_H
#define AFFINE_FUNCTION_GENERATOR_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <utility>

#include "linear_inequality.h"
#include "smtsolvers/SMTSolverInterface.h"

/**
 * @brief Gestionnaire d'UN template de fonction affine : f(x) = Σ prefix_i * vars[i] + prefix_const
 *
 * Chaque instance gère les variables SMT de coefficients d'une seule fonction affine.
 *
 * Utilisé par :
 *   - AffineTemplate (1 instance pour la ranking function)
 *   - NestedTemplate (1 instance par composante)
 *   - SupportingInvariantGenerator (1 instance par SI)
 *
 * Convention de nommage : prefix_0, prefix_1, ..., prefix_{n-1}, prefix_const
 */
class AffineFunctionGenerator {
public:
    /**
     * @param prefix   Préfixe des noms de variables SMT (ex: "RANKING_C", "RANK_0", "SUP_INVAR_0")
     * @param num_vars Nombre de variables de programme
     */
    AffineFunctionGenerator(const std::string& prefix, int num_vars);

    /**
     * @brief Déclare toutes les variables de coefficients dans le solveur SMT
     */
    void declareParameters(std::shared_ptr<SMTSolver> solver) const;

    /**
     * @brief Construit la LinearInequality représentant l'expression affine
     *
     * Retourne : Σ prefix_i * vars[i] + prefix_const
     *            avec strict=false, motzkin_coef=ANYTHING
     *
     * NOTE : la LinearInequality retournée n'est PAS encore une vraie inégalité
     * (strict et motzkin_coef seront ajustés par l'appelant selon le contexte).
     *
     * @param vars Variables SSA dans l'ordre (x_0, x_1, ..., x_{n-1})
     */
    LinearInequality generate(const std::vector<std::string>& vars) const;

    /**
     * @brief Retourne les noms des paramètres SMT dans l'ordre
     * [prefix_0, ..., prefix_{n-1}, prefix_const]
     */
    const std::vector<std::string>& getParamNames() const;

    /**
     * @brief Lit les valeurs des coefficients depuis le modèle SMT (après SAT)
     * Retourne les valeurs dans le même ordre que getParamNames()
     */
    std::vector<double> extractValues(std::shared_ptr<SMTSolver> solver) const;

    /**
     * @brief Lit les valeurs exactes comme rationnels (num, den) depuis le modele SMT
     */
    std::vector<std::pair<int64_t, int64_t>> extractRationals(std::shared_ptr<SMTSolver> solver) const;

    int getNumVars() const { return num_vars_; }

private:
    std::string prefix_;
    int num_vars_;
    std::vector<std::string> param_names_;  // [prefix_0, ..., prefix_{n-1}, prefix_const]
};

#endif // AFFINE_FUNCTION_GENERATOR_H
