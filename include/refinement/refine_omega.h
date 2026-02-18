#ifndef REFINE_OMEGA_H
#define REFINE_OMEGA_H

#include <memory>
#include <spot/twa/twagraph.hh>

#include "termination/generic_termination_synthesizer.h"
#include "termination/ranking_and_invariant_validator.h"
#include "refinement/predicate_encoder.h"
#include "refinement/interpolant_automaton_builder.h"
#include "templates/affine_template.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "lasso_program.h"

/**
 * @brief Algorithme Refine_ω (papier §3, Algorithm 1)
 * 
 * Pipeline complet :
 * 1. Synthèse RF + SI
 * 2. Validation + filtrage SI
 * 3. Encodage prédicats
 * 4. Construction A_I
 * 5. A_D ∩ A_I
 * 6. Emptiness check
 */
class RefineOmega {
public:
    struct Result {
        bool is_spurious;                    // Lasso spurieux ?
        spot::twa_graph_ptr refined_automaton; // A_D raffiné si spurious
        std::string error_message;
    };

    RefineOmega(std::shared_ptr<SMTSolver> solver);

    /**
     * @brief Exécute Refine_ω complet
     * 
     * @param lasso Programme lasso à analyser
     * @param A_D Automate abstraction (à raffiner)
     * @param template_config Configuration template (num_si_strict, num_si_nonstrict)
     * @return Result avec verdict spurious/réel
     */
    Result refine(
        const LassoProgram& lasso,
        spot::twa_graph_ptr A_D,
        int num_si_strict = 0,
        int num_si_non_strict = 1
    );

private:
    std::shared_ptr<SMTSolver> mSolver;
    PredicateEncoder mPredicateEncoder;
    InterpolantAutomatonBuilder mAutomatonBuilder;
};

#endif