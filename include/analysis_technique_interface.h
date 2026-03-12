#ifndef ANALYSIS_TECHNIQUE_INTERFACE_H
#define ANALYSIS_TECHNIQUE_INTERFACE_H

#include <memory>
#include <string>
#include <map>

#include "lasso_program.h"
#include "smtsolvers/SMTSolverInterface.h"

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

    std::map<std::string, int64_t> rf_witness;        // terminaison : coefficients RF
    std::map<std::string, double>  nt_witness_state;  // non-terminaison : état témoin

    ProofCertificate() = default;

    bool isConclusive() const {
        return status != AnalysisResult::UNKNOWN;
    }
};

/**
 * @brief Interface unifiée pour toutes les techniques d'analyse
 */
class AnalysisTechniqueInterface {
public:
    virtual ~AnalysisTechniqueInterface() = default;

    /**
     * @brief Initialise la technique avec le programme lasso
     * DOIT être appelé avant analyze()
     */
    virtual void init(const LassoProgram& lasso) = 0;

    /**
     * @brief Analyse le programme et retourne le verdict
     * @param solver Le solveur SMT à utiliser
     * @return AnalysisResult::TERMINATING, NON_TERMINATING, ou UNKNOWN
     */
    virtual AnalysisResult analyze(std::shared_ptr<SMTSolver> solver) = 0;

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
     * @brief Demande l'annulation de la technique (pour parallélisation)
     */
    virtual void cancel() {}

    /**
     * @brief Indique si la technique peut être annulée
     */
    virtual bool canBeCancelled() const { return true; }
};

#endif // ANALYSIS_TECHNIQUE_INTERFACE_H
