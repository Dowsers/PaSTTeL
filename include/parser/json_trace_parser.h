#ifndef JSON_TRACE_PARSER_H
#define JSON_TRACE_PARSER_H

#include <string>
#include <map>
#include <set>

#include "parser/smt_parser.h"
#include "linearization/formula_linearizer.h"
#include "rewriting/formula_rewriter.h"
#include "external/nlohmann/json.hpp"
#include "transition.h"
#include "lasso_program.h"

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

    static void convertLassoStringToLassoProgram(
                const std::string& stem_formula,
                const std::string& loop_formula,
                LassoProgram& lasso);


    static UltimateTransitionLine convertSMTFormula2ToLinearInequalities(
                const std::string& formula,
                LassoProgram& lasso);

    /**
     * Remove array variables from program_vars based on their sorts in var_sorts.
     * @param program_vars List of program variables to filter (modified in place)
     * @param var_sorts Mapping of variable name to its sort"
     */
    static void removeArrayVarsFromProgramVars(
                    std::vector<std::string>& program_vars,
                    const std::map<std::string, std::string>& var_sorts);
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
        FormulaRewriter* rewriter = nullptr);

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
