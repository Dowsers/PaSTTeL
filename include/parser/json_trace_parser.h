#ifndef JSON_TRACE_PARSER_H
#define JSON_TRACE_PARSER_H

#include <string>
#include <map>
#include <set>
#include "lasso_program.h"
#include "parser/smt_parser.h"
#include "linearization/formula_linearizer.h"
#include "external/nlohmann/json.hpp"

// Forward declarations
class RewriteDivision;

/**
 * Structure pour représenter une transition avec ses métadonnées
 * Cette structure correspond exactement à une ligne du fichier counter.txt
 */
struct UltimateTransitionLine {
    // Identifiant de la transition (ex: "L111")
    std::string label;
    
    // Formule SMT brute (ex: "(= v___tmp__now_16 v_now_12)")
    std::string formula;
    
    // Mapping : variable_de_programme → version_SSA pour les entrées
    // Exemple : {"now" → "v_now_12", "count_Counter" → "v_count_Counter_19"}
    std::map<std::string, std::string> in_vars;
    
    // Mapping : variable_de_programme → version_SSA pour les sorties
    std::map<std::string, std::string> out_vars;
    
    // Les contraintes parsées depuis la formule SMT (structure DNF complète)
    DNFFormula dnf;
    
    // Variables booléennes si présentes
    std::map<std::string, bool> bool_vars;

    // Variables SSA libres dans la formule (ni in_vars ni out_vars).
    // Typiquement les variables auxiliaires Ultimate (div_aux, mod_aux, etc.)
    // du mode PREPROCESSED LINEAR TRACE.  Elles doivent être déclarées dans
    // le solver mais n'ont pas de coefficient dans la fonction de ranking.
    std::vector<std::string> free_vars;
};

/**
 * JSON-based trace parser
 * Parses traces in JSON format to LassoProgram
 */
class JsonTraceParser {
public:
    /**
     * Parse JSON file to LassoProgram
     * @param filename Path to .json file
     * @return LassoProgram structure
     * @throws std::runtime_error if file cannot be opened or JSON is invalid
     */
    static LassoProgram parseToLasso(const std::string& filename);

private:
    /**
     * Parse a single transition from JSON object.
     * If a FormulaLinearizer is provided, function calls in the formula
     * are replaced by fresh variables before parsing to DNF.
     *
     * @param trans_json JSON object for one transition
     * @param linearizer Pointer to linearizer (nullptr to skip linearization)
     * @return UltimateTransitionLine structure
     */
    static UltimateTransitionLine parseTransition(
        const nlohmann::json& trans_json,
        FormulaLinearizer* linearizer = nullptr,
        RewriteDivision* div_rewriter = nullptr,
        const std::set<std::string>& bool_program_vars = {});

    /**
     * Parse variable mapping from JSON object
     * @param vars_json JSON object with var->ssa mappings
     * @return Map of variable name to SSA version
     */
    static std::map<std::string, std::string> parseVarsMapping(const nlohmann::json& vars_json);

    /**
     * Validate JSON structure
     * @param j Root JSON object
     * @throws std::runtime_error if structure is invalid
     */
    static void validateJsonStructure(const nlohmann::json& j);
};

#endif // JSON_TRACE_PARSER_H
