#ifndef TRANSITION_BUILDER_H
#define TRANSITION_BUILDER_H

#include <vector>
#include "lasso_program.h"

struct UltimateTransitionLine;

/**
 * TransitionBuilder - Builds a LinearTransition from a sequence of
 * UltimateTransitionLine entries by composing them sequentially.
 *
 * Uses InVars/OutVars metadata for SSA variable substitution during composition.
 * Used by JsonTraceParser (.json).
 */
class TransitionBuilder {
public:
    /**
     * Build a single LinearTransition by composing a sequence of transitions.
     *
     * For each consecutive pair Ti, Ti+1, the OutVars of Ti are matched
     * to the InVars of Ti+1 to build the substitution table.
     */
    static LinearTransition buildFromLines(
        const std::vector<UltimateTransitionLine>& lines
    );
};

#endif // TRANSITION_BUILDER_H
