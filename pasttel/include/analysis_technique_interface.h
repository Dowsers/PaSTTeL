#ifndef ANALYSIS_TECHNIQUE_INTERFACE_H
#define ANALYSIS_TECHNIQUE_INTERFACE_H

#include <memory>
#include <string>
#include <map>
#include <vector>

#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"
#include "termination/ranking_function.h"
#include "termination/supporting_invariant.h"

/**
 * @brief Verdict d'analyse retourné par analyze()
 */
enum class AnalysisResult {
    UNKNOWN,
    TERMINATING,
    NON_TERMINATING
};

/**
 * @brief Certificat de preuve retourné par getProof()
 *
 * Contient tous les détails de la dernière analyse effectuée.
 */
struct ProofCertificate {
    AnalysisResult status = AnalysisResult::UNKNOWN;
    std::string technique_name;
    std::string description;
    std::string proof_details;
    double execution_time_ms = 0.0;

    // Legacy, flattened fields (rf_witness = component [0] only, nt_witness_state
    // = lossy double cast) -- kept for printAnalysisReport() and the benchmark
    // script; prefer the structured fields below for anything else.
    std::map<std::string, Rational> rf_witness;
    std::map<std::string, double>  nt_witness_state;

    // Structured termination witness (status == TERMINATING): one component per
    // phase, guards (PiecewiseTemplate only), supporting invariants.
    std::vector<RankingFunction> ranking_functions;
    std::vector<RankingFunction> guards;
    std::vector<SupportingInvariant> supporting_invariants;

    // Structured non-termination witness (status == NON_TERMINATING), exact
    // Rational -- x_init, x_honda, GEVs/lambdas/nus (Leike & Heizmann, TACAS
    // 2018). FixpointTechnique only populates nt_state_honda.
    std::map<std::string, Rational> nt_state_init;
    std::map<std::string, Rational> nt_state_honda;
    std::vector<std::map<std::string, Rational>> nt_eigenvectors;
    std::vector<Rational> nt_lambdas;
    std::vector<Rational> nt_nus;

    ProofCertificate() = default;

    bool isConclusive() const {
        return status != AnalysisResult::UNKNOWN;
    }
};

/**
 * @brief Interface unifiée pour toutes les techniques d'analyse
 */
class AnalysisInterface {
public:
    virtual ~AnalysisInterface() = default;

    /**
     * @brief Initialise la technique avec le programme lasso
     * DOIT être appelé avant analyze()
     */
    virtual void init(const LassoProgram& lasso) = 0;

    /**
     * @brief Analyse le programme et retourne le verdict
     * @return AnalysisResult::TERMINATING, NON_TERMINATING, ou UNKNOWN
     */
    virtual AnalysisResult analyze() = 0;

    /**
     * @brief Retourne le certificat de preuve du dernier appel à analyze()
     */
    virtual ProofCertificate getProof() const = 0;

    /**
     * @brief Retourne le nom de la technique (pour logging)
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Valide la configuration de la technique
     */
    virtual bool validateConfiguration() const { return true; }

    /**
     * @brief Returns true if the technique needs a linearized LassoProgram.
     * Techniques returning false can run on the raw (non-linearized) lasso.
     */
    virtual bool requiresLinearization() const { return true; }

    /**
     * @brief Demande l'annulation de la technique (pour parallélisation)
     */
    virtual void cancel() {
        if (solver_) solver_->interrupt();
    }

    /**
     * @brief Indique si la technique peut être annulée
     */
    virtual bool canBeCancelled() const { return true; }

protected:
    SMTSolverInterface* solver_;
};

#endif // ANALYSIS_TECHNIQUE_INTERFACE_H
