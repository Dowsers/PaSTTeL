#ifndef REPORT_JSON_H
#define REPORT_JSON_H

#include "external/nlohmann/json.hpp"
#include "portfolio_orchestrator.h"

/**
 * @brief Sérialise un AnalysisReport (winner + all_results, champs structurés
 * inclus) en JSON, pour un consommateur programmatique -- Rational en
 * {"num": "...", "den": "..."} (chaînes, exact au-delà d'un double).
 */
nlohmann::json reportToJson(const AnalysisReport& report);

#endif // REPORT_JSON_H
