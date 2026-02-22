#ifndef TERMINATION_ARGUMENT_H
#define TERMINATION_ARGUMENT_H

#include <vector>

#include "termination/ranking_function.h"
#include "termination/supporting_invariant.h"

/**
 * @brief Argument de terminaison : résultat final de la synthèse
 *
 * Regroupe la fonction de ranking et les supporting invariants
 * synthétisés pour un programme lasso donné.
 *
 */
struct TerminationArgument {
    RankingFunction ranking_function;                   // première composante (rétrocompatibilité)
    std::vector<RankingFunction> components;            // toutes les composantes (1 pour Affine, k pour Lex/Nested)
    std::vector<SupportingInvariant> supporting_invariants;

    TerminationArgument() = default;

    TerminationArgument(
        const RankingFunction& rf,
        const std::vector<SupportingInvariant>& sis)
        : ranking_function(rf)
        , supporting_invariants(sis)
    {}

    bool isValid() const {
        return !ranking_function.coefficients.empty()
            || ranking_function.constant != 0.0;
    }
};

#endif // TERMINATION_ARGUMENT_H
