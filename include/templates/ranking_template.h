#ifndef RANKING_TEMPLATE_H
#define RANKING_TEMPLATE_H

#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "lasso_program.h"
#include "linear_inequality.h"
#include "termination/ranking_function.h"
#include "smtsolvers/SMTSolverInterface.h"


/**
 * @brief Interface abstraite pour tous les templates de ranking functions
 *
 * Architecture modulaire permettant :
 * - Ajout facile de nouveaux templates (Affine, Nested, Lexicographic, etc.)
 * - Execution parallele de plusieurs templates
 * - Separation claire entre generation de contraintes et resolution SMT
 */
class RankingTemplate {
public:
    /**
     * @brief Contexte Motzkin - une contrainte universelle a verifier
     * Represente: forall x,x' (premises -> conclusion)
     * Transforme par Motzkin en: exists lambda >= 0 (sum lambda_i * premise_i - conclusion >= 0)
     */
    struct MotzkinContext {
        std::vector<LinearInequality> constraints;  // Toutes contraintes (premises + negation conclusion)
        std::string annotation;                     // Description pour debug/logging

        MotzkinContext() = default;
        MotzkinContext(const std::vector<LinearInequality>& c, const std::string& a)
            : constraints(c), annotation(a) {}
    };

    /**
     * @brief Informations sur les parametres du template pour SMT
     * (uniquement les parametres de la ranking function -- les SI sont dans SupportingInvariantGenerator)
     */
    struct TemplateParameters {
        std::vector<std::string> ranking_params;    // Parametres de la ranking function
        std::string delta_param;                    // Nom du parametre delta
        int delta_value;                            // Valeur de delta

        int getTotalParameterCount() const {
            return ranking_params.size() + (delta_param.empty() ? 0 : 1);
        }
    };

    virtual ~RankingTemplate() = default;

    // ========================================================================
    // METHODES ABSTRAITES - A implementer par chaque template
    // ========================================================================

    /**
     * @brief Initialise le template avec le programme lasso
     * DOIT etre appele avant getConstraintsDec/Bounded/declareParameters
     */
    virtual void init(const LassoProgram& lasso) = 0;

    /**
     * @brief Retourne les informations sur les parametres SMT
     */
    virtual TemplateParameters getParameters() const = 0;

    /**
     * @brief Extrait les composantes de la ranking function depuis le modele SMT
     */
    virtual std::vector<RankingFunction> extractRankingFunctions(
        std::shared_ptr<SMTSolver> solver,
        const std::vector<std::string>& program_vars) const = 0;

    /**
     * @brief Retourne le nom du template (pour logging)
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Retourne une description du template
     */
    virtual std::string getDescription() const = 0;

    // ========================================================================
    // INTERFACE PRINCIPALE
    // ========================================================================

    /**
     * @brief Retourne les CONCLUSIONS POSITIVES de decroissance (non negees).
     *
     * AffineTemplate :  1 LinearInequality  [f(x)-f(x') >= delta, strict=false, ONE]
     * NestedTemplate(k): k LinearInequality [i=0: f0-f0' >= delta, i>0: fi-fi'+f_{i-1} >= 0]
     *
     * Le synthesizer negera ces conclusions et construira les contextes Motzkin complets.
     */
    virtual std::vector<LinearInequality> getConstraintsDec(
        const std::vector<std::string>& /*in_vars*/,
        const std::vector<std::string>& /*out_vars*/) const
    {
        throw std::logic_error(getName() + "::getConstraintsDec() not implemented");
    }

    /**
     * @brief Retourne la CONCLUSION POSITIVE de bornage (non negee).
     *
     * AffineTemplate  : f(x) >= 0     (strict=false, ONE)
     * NestedTemplate  : f_{n-1}(x) >= 0 (strict=false, ONE)
     */
    virtual LinearInequality getConstraintsBounded(
        const std::vector<std::string>& /*in_vars*/) const
    {
        throw std::logic_error(getName() + "::getConstraintsBounded() not implemented");
    }

    /**
     * @brief Declare les parametres SMT du template dans le solveur.
     *
     * Implementation par defaut : utilise getParameters().
     * AffineTemplate / NestedTemplate : surchargent avec AffineFunctionGenerator.
     */
    virtual void declareParameters(std::shared_ptr<SMTSolver> solver) const {
        auto params = getParameters();
        for (const auto& p : params.ranking_params) {
            solver->declareVariable(p, "Real");
        }
        if (!params.delta_param.empty()) {
            solver->declareVariable(params.delta_param, "Real");
            std::ostringstream oss;
            oss << "(>= " << params.delta_param << " " << params.delta_value << ")";
            solver->addAssertion(oss.str());
        }
    }

    // ========================================================================
    // METHODES OPTIONNELLES
    // ========================================================================

    /**
     * @brief Interface legacy -- conservee pour que LexicographicTemplate compile.
     * A remplacer par getConstraintsDec/Bounded dans les prochains templates.
     */
    virtual std::vector<MotzkinContext> getConstraints(
        const std::vector<LinearInequality>& /*si_preconditions*/ = {}) const
    {
        throw std::logic_error(getName() + "::getConstraints() not implemented");
    }

    virtual bool validateConfiguration() const { return true; }

    virtual void printInfo() const = 0;
};

#endif // RANKING_TEMPLATE_H
