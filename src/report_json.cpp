#include "report_json.h"

using json = nlohmann::json;

namespace {

// Rational -> {"num": "...", "den": "..."} (strings: num/den are boost::multiprecision::
// cpp_int, arbitrary precision -- a JSON number would silently truncate beyond
// int64/double range).
json jsonOfRational(const Rational& r) {
    return json{{"num", toStringBigInt(r.num)}, {"den", toStringBigInt(r.den)}};
}

json jsonOfCoefficients(const std::map<std::string, Rational>& coeffs) {
    json j = json::object();
    for (const auto& [var, coef] : coeffs) {
        j[var] = jsonOfRational(coef);
    }
    return j;
}

json jsonOfRankingFunction(const RankingFunction& rf) {
    return json{
        {"coefficients", jsonOfCoefficients(rf.coefficients)},
        {"constant", jsonOfRational(rf.constant)},
        {"delta", jsonOfRational(rf.delta)},
    };
}

json jsonOfSupportingInvariant(const SupportingInvariant& si) {
    return json{
        {"coefficients", jsonOfCoefficients(si.coefficients)},
        {"constant", jsonOfRational(si.constant)},
        {"is_strict", si.is_strict},
    };
}

std::string jsonOfStatus(AnalysisResult status) {
    switch (status) {
        case AnalysisResult::TERMINATING:     return "TERMINATING";
        case AnalysisResult::NON_TERMINATING: return "NON_TERMINATING";
        case AnalysisResult::UNKNOWN:
        default:                              return "UNKNOWN";
    }
}

json jsonOfProofCertificate(const ProofCertificate& p) {
    json j;
    j["status"] = jsonOfStatus(p.status);
    j["technique_name"] = p.technique_name;
    j["description"] = p.description;
    j["proof_details"] = p.proof_details;
    j["execution_time_ms"] = p.execution_time_ms;

    // Legacy flattened fields, kept for consumers still on the old shape.
    j["rf_witness"] = jsonOfCoefficients(p.rf_witness);
    j["nt_witness_state"] = p.nt_witness_state;

    // Structured termination witness.
    json ranking_functions = json::array();
    for (const auto& rf : p.ranking_functions) ranking_functions.push_back(jsonOfRankingFunction(rf));
    j["ranking_functions"] = ranking_functions;

    json guards = json::array();
    for (const auto& g : p.guards) guards.push_back(jsonOfRankingFunction(g));
    j["guards"] = guards;

    json supporting_invariants = json::array();
    for (const auto& si : p.supporting_invariants) supporting_invariants.push_back(jsonOfSupportingInvariant(si));
    j["supporting_invariants"] = supporting_invariants;

    // Structured non-termination witness, exact.
    j["nt_state_init"] = jsonOfCoefficients(p.nt_state_init);
    j["nt_state_honda"] = jsonOfCoefficients(p.nt_state_honda);

    json eigenvectors = json::array();
    for (const auto& gev : p.nt_eigenvectors) eigenvectors.push_back(jsonOfCoefficients(gev));
    j["nt_eigenvectors"] = eigenvectors;

    json lambdas = json::array();
    for (const auto& l : p.nt_lambdas) lambdas.push_back(jsonOfRational(l));
    j["nt_lambdas"] = lambdas;

    json nus = json::array();
    for (const auto& n : p.nt_nus) nus.push_back(jsonOfRational(n));
    j["nt_nus"] = nus;

    return j;
}

}  // namespace

json reportToJson(const AnalysisReport& report) {
    json j;
    j["overall_result"] = report.overall_result;
    j["total_time_ms"] = report.total_time_ms;
    j["terminating_time_ms"] = report.terminating_time_ms;
    j["nonterminating_time_ms"] = report.nonterminating_time_ms;
    j["registered_techniques"] = report.registered_techniques;
    j["winner"] = jsonOfProofCertificate(report.winner);

    json all_results = json::array();
    for (const auto& r : report.all_results) all_results.push_back(jsonOfProofCertificate(r));
    j["all_results"] = all_results;

    return j;
}
