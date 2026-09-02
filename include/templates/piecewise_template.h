#ifndef PIECEWISE_TEMPLATE_H
#define PIECEWISE_TEMPLATE_H

#include <memory>
#include <vector>
#include "templates/ranking_template.h"
#include "termination/affine_function_generator.h"

/**
 * @brief Piecewise ranking function template (k pieces, guard h_i + local rank f_i).
 *
 * Splits the state space into k pieces via affine guards h_0..h_{k-1}: wherever
 * h_i(x)>=0 holds, the LOCAL function f_i must be bounded and decrease. Pieces
 * may overlap (only coverage is required, not mutual exclusivity) -- as
 * opposed to NestedTemplate/LexicographicTemplate (order between components)
 * or MultiphaseTemplate (fixed temporal sequence of phases), Piecewise has no
 * notion of order at all: it's a pure case split.
 *
 * Positive conclusions (before negation), each already in disjunctive form
 * (implication rewritten as OR) so it maps directly onto ConclusionPart:
 *
 * phi_bound_i (i=0..k-1) : -h_i(x)>0  OR  f_i(x)>=0            (2 atoms)
 * phi_decr_i  (i=0..k-1) : -h_i(x)>0  OR  f_i(x)-f_i(x')>=delta_i  (2 atoms)
 * phi_exhaustive         : h_0(x)>=0 OR ... OR h_{k-1}(x)>=0   (k atoms, every
 *                          transition must land in at least one piece)
 *
 * Local supporting invariants (phi1/phi2) and Motzkin context construction
 * are delegated to GenericTerminationSynthesizer, same pipeline as
 * AffineTemplate/NestedTemplate/LexicographicTemplate/MultiphaseTemplate.
 */
class PiecewiseTemplate : public RankingTemplate {
public:
    explicit PiecewiseTemplate(int num_pieces = 2, int delta_value = 0);

    void init(const LassoProgram& lasso) override;

    std::vector<ConclusionPart> getConstraintsDec(
        const std::vector<std::string>& in_vars,
        const std::vector<std::string>& out_vars) const override;

    std::vector<ConclusionPart> getConstraintsBounded(
        const std::vector<std::string>& in_vars) const override;

    /**
     * @brief Declares every piece's rank + guard coefficients, and EVERY
     * delta_params_[i] with a strict `> delta_value_` assertion (all pieces,
     * not just the first -- required for soundness of phi_decr_i's escape atom).
     */
    void declareParameters(SMTSolverInterface* solver) const override;

    TemplateParameters getParameters() const override;

    std::vector<RankingFunction> extractRankingFunctions(
        SMTSolverInterface* solver,
        const std::vector<std::string>& program_vars) const override;

    /** @brief Extrait les gardes h_i (une par morceau), pour la validation. */
    std::vector<RankingFunction> extractGuards(
        SMTSolverInterface* solver,
        const std::vector<std::string>& program_vars) const override;

    std::string getName() const override {
        return std::to_string(num_pieces_) + "-piecewise";
    }

    std::string getDescription() const override;

    void printInfo() const override;

    int getNumPieces() const { return num_pieces_; }

private:
    int num_pieces_;
    int delta_value_;

    LassoProgram lasso_;
    bool initialized_;

    std::vector<std::unique_ptr<AffineFunctionGenerator>> rank_generators_;   // f_i
    std::vector<std::unique_ptr<AffineFunctionGenerator>> guard_generators_;  // h_i
    std::vector<std::string> delta_params_;  // one delta per piece
};

#endif // PIECEWISE_TEMPLATE_H
