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
    std::vector<RankingFunction> ranking_functions;            // toutes les composantes (1 pour Affine, k pour Lex/Nested)
    std::vector<SupportingInvariant> supporting_invariants;

    TerminationArgument() = default;
};

#endif // TERMINATION_ARGUMENT_H
