#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <memory>
#include <regex>

#include "parser/json_trace_parser.h"
#include "parser/transition_builder.h"
#include "parser/smt_parser.h"
#include "linearization/formula_linearizer.h"
#include "linearization/uf_handler.h"
#include "linearization/array_handler.h"
#include "rewriting/rewrite_let.h"
#include "rewriting/rewrite_division.h"
#include "rewriting/rewrite_equality.h"
#include "rewriting/rewrite_booleans.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// MAIN PARSING FUNCTION
// ============================================================================

LassoProgram JsonTraceParser::parseToLasso(const std::string& filename) {
    LassoProgram lasso;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n==================================================================" << std::endl;
        std::cout << "=== Parsing JSON trace file: " << filename << " ===" << std::endl;
        std::cout << "==================================================================\n" << std::endl;
    }

    // 1. Read and parse JSON file
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open JSON trace file: " + filename);
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }
    file.close();

    // 2. Validate structure
    validateJsonStructure(j);

    // 3. Extract program variables
    if (j.contains("program_vars") && j["program_vars"].is_array()) {
        for (const auto& var : j["program_vars"]) {
            lasso.program_vars.push_back(var.get<std::string>());
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "Program variables: " << lasso.program_vars.size() << std::endl;
    }

    // 3b. Extract constants (if present)
    if (j.contains("constants") && j["constants"].is_object()) {
        for (auto it = j["constants"].begin(); it != j["constants"].end(); ++it) {
            DeclaredConstant constant;
            constant.name = it.key();

            if (it.value().is_object()) {
                // Format: {"name": {"type": "Int", "value": "..."}}
                if (it.value().contains("type")) {
                    constant.type = it.value()["type"].get<std::string>();
                }
                if (it.value().contains("value")) {
                    if (it.value()["value"].is_string()) {
                        constant.value = it.value()["value"].get<std::string>();
                    } else {
                        constant.value = it.value()["value"].dump();
                    }
                }
            } else if (it.value().is_string()) {
                // Format simplifié: {"name": "Int"}
                constant.type = it.value().get<std::string>();
            }

            lasso.constants.push_back(constant);
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "Constants declared: " << lasso.constants.size() << std::endl;
        }
    }

    // 3b2. Extract array variables (if present)
    // Format: "array_vars": {"balance": "(Array Int Int)", "addr": "Int"}
    // Variables with Array sorts are tracked in var_sorts for correct declaration
    bool has_arrays = false;
    if (j.contains("array_vars") && j["array_vars"].is_object()) {
        for (auto it = j["array_vars"].begin(); it != j["array_vars"].end(); ++it) {
            std::string var_name = it.key();
            std::string var_sort = it.value().get<std::string>();

            // Store the sort for this variable
            lasso.var_sorts[var_name] = var_sort;

            // Track if any variable is actually an array
            if (var_sort.substr(0, 6) == "(Array") {
                has_arrays = true;
            }
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "Array/typed variables declared: " << lasso.var_sorts.size() << std::endl;
            for (const auto& [name, sort] : lasso.var_sorts) {
                std::cout << "  " << name << " : " << sort << std::endl;
            }
        }
    }

    // 3b3. Extract var_types (if present)
    // Format: "var_types": {"x": "Int", "flag": "Bool", "arr": "(Array Int Int)"}
    // Populates var_sorts for all typed variables; merges with array_vars
    std::set<std::string> bool_vars;
    if (j.contains("var_types") && j["var_types"].is_object()) {
        for (auto it = j["var_types"].begin(); it != j["var_types"].end(); ++it) {
            std::string var_name = it.key();
            std::string var_type = it.value().get<std::string>();

            lasso.var_sorts[var_name] = var_type;

            if (var_type == "Bool") {
                bool_vars.insert(var_name);
            }
            if (var_type.substr(0, 6) == "(Array") {
                has_arrays = true;
            }
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "Variable types loaded: " << lasso.var_sorts.size() << std::endl;
            if (!bool_vars.empty()) {
                std::cout << "  Boolean variables: ";
                for (const auto& bv : bool_vars) std::cout << bv << " ";
                std::cout << std::endl;
            }
        }
    }

    // 3b3b. Filter Array vars from program_vars (no linear coefficient possible).
    // Bool vars are KEPT — their 0/1 bounds are injected by rewriteWithBounds.
    {
        auto it = lasso.program_vars.begin();
        while (it != lasso.program_vars.end()) {
            auto sort_it = lasso.var_sorts.find(*it);
            if (sort_it != lasso.var_sorts.end() &&
                sort_it->second.find("Array") != std::string::npos) {
                if (VERBOSITY == VerbosityLevel::VERBOSE)
                    std::cout << "  Filtered Array var from program_vars: "
                              << *it << " (" << sort_it->second << ")" << std::endl;
                it = lasso.program_vars.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 3b4. Compute integer_mode from var_sorts
    // integer_mode = true if ANY variable has genuine "Int" type (not Bool rewritten to Int)
    // This ensures geometric nontermination declares all coefficients as Int for soundness.
    for (const auto& [var_name, var_sort] : lasso.var_sorts) {
        if (var_sort == "Int") {
            lasso.integer_mode = true;
            break;
        }
    }
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "Integer mode: " << (lasso.integer_mode ? "ON" : "OFF") << std::endl;
    }

    // 3c. Extract uninterpreted functions (if present)
    if (j.contains("functions") && j["functions"].is_array()) {
        for (const auto& func_json : j["functions"]) {
            UninterpretedFunction func;

            if (func_json.contains("name")) {
                func.name = func_json["name"].get<std::string>();
            }
            if (func_json.contains("signature")) {
                func.signature = func_json["signature"].get<std::string>();
            }

            lasso.functions.push_back(func);
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "Uninterpreted functions declared: " << lasso.functions.size() << std::endl;
        }
    }

    // 3d. Extract axioms (if present)
    if (j.contains("axioms") && j["axioms"].is_array()) {
        for (const auto& axiom_json : j["axioms"]) {
            Axiom axiom;

            if (axiom_json.is_object()) {
                // Format: {"formula": "...", "description": "..."}
                if (axiom_json.contains("formula")) {
                    axiom.formula = axiom_json["formula"].get<std::string>();
                }
                if (axiom_json.contains("description")) {
                    axiom.description = axiom_json["description"].get<std::string>();
                }
            } else if (axiom_json.is_string()) {
                // Format simplifié: "formula"
                axiom.formula = axiom_json.get<std::string>();
            }

            if (!axiom.formula.empty()) {
                lasso.axioms.push_back(axiom);
            }
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "Axioms declared: " << lasso.axioms.size() << std::endl;
        }
    }

    // 3e. Check for div/mod operations in formulas
    // These are non-linear and need to be abstracted by FormulaLinearizer
    bool has_divmod = false;
    auto checkDivMod = [](const nlohmann::json& transitions) -> bool {
        if (!transitions.is_array()) return false;
        for (const auto& trans : transitions) {
            if (trans.contains("formula") && trans["formula"].is_string()) {
                std::string formula = trans["formula"].get<std::string>();
                if (formula.find("(div ") != std::string::npos ||
                    formula.find("(mod ") != std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    };

    if (j.contains("stem")) has_divmod = has_divmod || checkDivMod(j["stem"]);
    if (j.contains("loop")) has_divmod = has_divmod || checkDivMod(j["loop"]);

    if (has_divmod && VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "Detected div/mod operations in formulas" << std::endl;
    }

    // 3f. Create RewriteDivision for div/mod rewriting
    // This replaces (div x y) and (mod x y) with auxiliary variables
    // and conjoins equivalent linear constraints directly into the formula.
    std::unique_ptr<RewriteDivision> div_rewriter;
    if (has_divmod) {
        div_rewriter = std::make_unique<RewriteDivision>();

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "RewriteDivision: will rewrite div/mod with linear constraints" << std::endl;
        }
    }

    // 3h. Create FormulaLinearizer with appropriate handlers
    // The linearizer replaces non-linear terms with fresh variables
    // so that the formulas become linear for Motzkin transformation.
    std::unique_ptr<FormulaLinearizer> linearizer;
    bool needs_linearizer = !lasso.functions.empty() || has_arrays;

    if (needs_linearizer) {
        linearizer = std::make_unique<FormulaLinearizer>();

        // Add UFHandler for uninterpreted functions
        if (!lasso.functions.empty()) {
            std::set<std::string> function_names;
            for (const auto& func : lasso.functions) {
                function_names.insert(func.name);
            }
            auto uf_handler = std::make_unique<UFHandler>(function_names);

            // Set return sorts from function signatures
            for (const auto& func : lasso.functions) {
                // Signature format: "(Int Int) Int" -> return type is last token
                std::string sig = func.signature;
                size_t last_space = sig.rfind(' ');
                if (last_space != std::string::npos) {
                    std::string return_sort = sig.substr(last_space + 1);
                    if (!return_sort.empty() && return_sort.back() == ')') {
                        return_sort.pop_back();
                    }
                    uf_handler->setFunctionSort(func.name, return_sort);
                }
            }

            linearizer->addHandler(std::move(uf_handler));

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaLinearizer: UFHandler for " << function_names.size()
                          << " function(s)" << std::endl;
            }
        }

        // Add ArrayHandler for array select operations
        if (has_arrays) {
            auto array_handler = std::make_unique<ArrayHandler>();
            linearizer->addHandler(std::move(array_handler));

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaLinearizer: ArrayHandler added for select linearization" << std::endl;
            }
        }

    }

    // 4. Parse STEM transitions
    std::vector<UltimateTransitionLine> stem_lines;
    if (j.contains("stem") && j["stem"].is_array()) {
        for (const auto& trans_json : j["stem"]) {
            stem_lines.push_back(parseTransition(trans_json, linearizer.get(), div_rewriter.get(), bool_vars));
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "Stem transitions: " << stem_lines.size() << std::endl;
    }

    // 5. Parse LOOP transitions
    std::vector<UltimateTransitionLine> loop_lines;
    if (j.contains("loop") && j["loop"].is_array()) {
        for (const auto& trans_json : j["loop"]) {
            loop_lines.push_back(parseTransition(trans_json, linearizer.get(), div_rewriter.get(), bool_vars));
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "Loop transitions: " << loop_lines.size() << std::endl;
    }

    // 6. Build transitions using TransitionBuilder (composition logic)
    if (!stem_lines.empty()) {
        lasso.stem = TransitionBuilder::buildFromLines(stem_lines);
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "STEM built with " << lasso.stem.var_to_ssa_in.size() << " input vars" << std::endl;
        }
    }

    if (!loop_lines.empty()) {
        lasso.loop = TransitionBuilder::buildFromLines(loop_lines);
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "LOOP built with " << lasso.loop.var_to_ssa_out.size() << " output vars" << std::endl;
        }
    }

    // 7. Connect STEM->LOOP (same logic as text parser lines 59-130)
    if (!stem_lines.empty() && !loop_lines.empty()) {
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "\n=== Connecting STEM to LOOP ===" << std::endl;
        }

        std::map<std::string, std::string> substitution;
        for (const auto& [var_prog, ssa_out_stem] : lasso.stem.var_to_ssa_out) {
            auto it = lasso.loop.var_to_ssa_in.find(var_prog);
            if (it != lasso.loop.var_to_ssa_in.end()) {
                std::string ssa_in_loop = it->second;
                if (ssa_in_loop != ssa_out_stem) {
                    substitution[ssa_in_loop] = ssa_out_stem;
                }
            }
        }

        // Apply substitution to LOOP constraints
        if (!substitution.empty()) {
            for (auto& poly : lasso.loop.polyhedra) {
                for (auto& ineq : poly) {
                    // Substitute in coefficients
                    std::map<std::string, AffineTerm> new_coeffs;
                    for (const auto& [var_ssa, coef] : ineq.coefficients) {
                        std::string new_var = var_ssa;
                        size_t bracket = var_ssa.find('[');
                        if (bracket != std::string::npos) {
                            std::string arr = var_ssa.substr(0, bracket);
                            std::string idx = var_ssa.substr(bracket + 1, var_ssa.find(']') - bracket - 1);
                            auto it_arr = substitution.find(arr);
                            auto it_idx = substitution.find(idx);
                            if (it_arr != substitution.end()) arr = it_arr->second;
                            if (it_idx != substitution.end()) idx = it_idx->second;
                            new_var = arr + "[" + idx + "]";
                        } else {
                            auto it = substitution.find(var_ssa);
                            if (it != substitution.end()) new_var = it->second;
                        }
                        new_coeffs[new_var] = coef;
                    }
                    ineq.coefficients = new_coeffs;

                    // Substitute in constant.coefficients
                    std::map<std::string, double> new_const_coeffs;
                    for (const auto& [var_ssa, val] : ineq.constant.coefficients) {
                        std::string new_var = var_ssa;
                        size_t bracket = var_ssa.find('[');
                        if (bracket != std::string::npos) {
                            std::string arr = var_ssa.substr(0, bracket);
                            std::string idx = var_ssa.substr(bracket + 1, var_ssa.find(']') - bracket - 1);
                            auto it_arr = substitution.find(arr);
                            auto it_idx = substitution.find(idx);
                            if (it_arr != substitution.end()) arr = it_arr->second;
                            if (it_idx != substitution.end()) idx = it_idx->second;
                            new_var = arr + "[" + idx + "]";
                        } else {
                            auto it = substitution.find(var_ssa);
                            if (it != substitution.end()) new_var = it->second;
                        }
                        new_const_coeffs[new_var] = val;
                    }
                    ineq.constant.coefficients = new_const_coeffs;
                }
            }

            // Update var_to_ssa_in of loop
            for (auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in) {
                auto it = substitution.find(ssa_in);
                if (it != substitution.end()) {
                    ssa_in = it->second;
                }
            }

            // Update var_to_ssa_out of loop (for identity variables where in_ssa == out_ssa)
            for (auto& [var_prog, ssa_out] : lasso.loop.var_to_ssa_out) {
                auto it = substitution.find(ssa_out);
                if (it != substitution.end()) {
                    ssa_out = it->second;
                }
            }
        }
    }

    // 7b. Ensure all program_vars have SSA mappings in both stem and loop
    //     If a program variable is missing from in_vars or out_vars of the loop
    //     (e.g. it's only written but not read), generate a fresh SSA variable.
    //     Without this, getSSAVar() would crash on missing entries.
    {
        static int fresh_counter = 0;
        auto ensureMapping = [&](std::map<std::string, std::string>& mapping,
                                 const std::string& prog_var, const std::string& prefix) {
            if (mapping.find(prog_var) == mapping.end()) {
                std::string fresh = "v_" + prog_var + "_fresh_" + prefix + "_" + std::to_string(fresh_counter++);
                mapping[prog_var] = fresh;
                if (VERBOSITY == VerbosityLevel::VERBOSE) {
                    std::cout << "  Generated fresh SSA var: " << fresh
                              << " for " << prog_var << " (" << prefix << ")" << std::endl;
                }
            }
        };

        for (const auto& var : lasso.program_vars) {
            ensureMapping(lasso.loop.var_to_ssa_in, var, "loop_in");
            ensureMapping(lasso.loop.var_to_ssa_out, var, "loop_out");
            if (!lasso.stem.polyhedra.empty()) {
                ensureMapping(lasso.stem.var_to_ssa_in, var, "stem_in");
                ensureMapping(lasso.stem.var_to_ssa_out, var, "stem_out");
            }
        }
    }

    // 7c. (supprimé) : Les variables non modifiées dans le loop (in == out) gardent
    //     le même nom SSA. Le Motzkin gère correctement ce cas : le coefficient de
    //     f(x') - f(x) pour une variable constante est zéro, ce qui est correct.

    // 8a. Store RewriteDivision auxiliary variables FIRST
    // These must be declared before linearizer abstractions because the linearizer's
    // original_call may reference div_aux/mod_aux vars (e.g., "(= nlmul__0 (* div_aux_0 y))").
    if (div_rewriter && !div_rewriter->getAuxVars().empty()) {
        for (const auto& abs : div_rewriter->getAuxVars()) {
            lasso.function_abstractions.push_back(abs);
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "\nRewriteDivision auxiliary variables: "
                      << div_rewriter->getAuxVars().size() << std::endl;
            for (const auto& abs : div_rewriter->getAuxVars()) {
                std::cout << "  " << abs.fresh_var << " (" << abs.sort << ")" << std::endl;
            }
        }
    }

    // 8b. Store linearizer function abstractions with composition substitutions applied
    if (linearizer && !linearizer->getAbstractions().empty()) {
        auto lin_abstractions = linearizer->getAbstractions();
        for (auto& abs : lin_abstractions) {
            lasso.function_abstractions.push_back(abs);
        }

        // Build the full substitution map from all transition compositions.
        // When transitions [T1, T2] are composed, T2.in_vars are substituted
        // by T1.out_vars. We need to apply these same substitutions to the
        // original_call in each function abstraction.
        std::map<std::string, std::string> composition_subst;

        // Collect substitutions from loop transitions composition
        if (loop_lines.size() > 1) {
            for (size_t i = 0; i + 1 < loop_lines.size(); ++i) {
                const auto& prev_out = loop_lines[i].out_vars;
                const auto& next_in  = loop_lines[i + 1].in_vars;
                for (const auto& [var_prog, ssa_in_next] : next_in) {
                    auto it = prev_out.find(var_prog);
                    if (it != prev_out.end() && it->second != ssa_in_next) {
                        composition_subst[ssa_in_next] = it->second;
                    }
                }
            }
        }

        // Collect substitutions from stem transitions composition
        if (stem_lines.size() > 1) {
            for (size_t i = 0; i + 1 < stem_lines.size(); ++i) {
                const auto& prev_out = stem_lines[i].out_vars;
                const auto& next_in  = stem_lines[i + 1].in_vars;
                for (const auto& [var_prog, ssa_in_next] : next_in) {
                    auto it = prev_out.find(var_prog);
                    if (it != prev_out.end() && it->second != ssa_in_next) {
                        composition_subst[ssa_in_next] = it->second;
                    }
                }
            }
        }

        // Collect substitutions from stem→loop connection (step 7)
        if (!stem_lines.empty() && !loop_lines.empty()) {
            for (const auto& [var_prog, ssa_out_stem] : lasso.stem.var_to_ssa_out) {
                // Find the original loop first in_var before step 7 substitution
                // loop_lines[0].in_vars has the original SSA names
                auto it = loop_lines[0].in_vars.find(var_prog);
                if (it != loop_lines[0].in_vars.end() && it->second != ssa_out_stem) {
                    composition_subst[it->second] = ssa_out_stem;
                }
            }
        }

        // Apply substitutions to each abstraction's original_call
        if (!composition_subst.empty()) {
            for (auto& abs : lasso.function_abstractions) {
                for (const auto& [old_var, new_var] : composition_subst) {
                    // Replace whole-word occurrences of old_var by new_var
                    // in the original_call S-expression
                    size_t pos = 0;
                    while ((pos = abs.original_call.find(old_var, pos)) != std::string::npos) {
                        // Check word boundary: char before must be space or '('
                        // and char after must be space or ')'
                        bool start_ok = (pos == 0) ||
                            abs.original_call[pos - 1] == ' ' ||
                            abs.original_call[pos - 1] == '(';
                        size_t end_pos = pos + old_var.size();
                        bool end_ok = (end_pos == abs.original_call.size()) ||
                            abs.original_call[end_pos] == ' ' ||
                            abs.original_call[end_pos] == ')';

                        if (start_ok && end_ok) {
                            abs.original_call.replace(pos, old_var.size(), new_var);
                            pos += new_var.size();
                        } else {
                            pos += old_var.size();
                        }
                    }
                }
            }
        }

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "\nFunction abstractions (post-composition): "
                      << lasso.function_abstractions.size() << std::endl;
            for (const auto& abs : lasso.function_abstractions) {
                std::cout << "  " << abs.fresh_var << " = " << abs.original_call
                          << " (" << abs.sort << ")" << std::endl;
            }
        }
    }

    // 8c. Register free_vars from all transitions as function_abstractions (Int, no assertion).
    // These are SSA variables present in the formula but not in in_vars/out_vars —
    // typically Ultimate's precomputed div_aux/mod_aux variables from PREPROCESSED traces.
    // They must be declared in the solver but carry no ranking-function coefficient.
    {
        std::set<std::string> already_declared;
        for (const auto& abs : lasso.function_abstractions) {
            already_declared.insert(abs.fresh_var);
        }
        auto registerFreeVars = [&](const std::vector<UltimateTransitionLine>& lines) {
            for (const auto& line : lines) {
                for (const auto& fv : line.free_vars) {
                    if (already_declared.insert(fv).second) {
                        FunctionAbstraction abs;
                        abs.fresh_var = fv;
                        abs.sort = "Int";
                        abs.original_call = "";
                        lasso.function_abstractions.push_back(abs);
                        if (VERBOSITY == VerbosityLevel::VERBOSE) {
                            std::cout << "  Free aux var declared: " << fv << " (Int)" << std::endl;
                        }
                    }
                }
            }
        };
        registerFreeVars(stem_lines);
        registerFreeVars(loop_lines);
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n=== LassoProgram constructed successfully ===" << std::endl;
    }

    return lasso;
}

// ============================================================================
// PARSE SINGLE TRANSITION
// ============================================================================

UltimateTransitionLine JsonTraceParser::parseTransition(
    const nlohmann::json& trans_json,
    FormulaLinearizer* linearizer,
    RewriteDivision* div_rewriter,
    const std::set<std::string>& bool_program_vars) {
    UltimateTransitionLine trans;

    // Extract source label
    if (trans_json.contains("source")) {
        trans.label = trans_json["source"].get<std::string>();
    }

    // Extract formula
    if (trans_json.contains("formula")) {
        trans.formula = trans_json["formula"].get<std::string>();
    }

    // Parse in_vars
    if (trans_json.contains("in_vars") && trans_json["in_vars"].is_object()) {
        trans.in_vars = parseVarsMapping(trans_json["in_vars"]);
    }

    // Parse out_vars
    if (trans_json.contains("out_vars") && trans_json["out_vars"].is_object()) {
        trans.out_vars = parseVarsMapping(trans_json["out_vars"]);
    }

    // Parse aux_vars (free SSA variables present in formula but not in in_vars/out_vars)
    if (trans_json.contains("aux_vars") && trans_json["aux_vars"].is_array()) {
        for (const auto& v : trans_json["aux_vars"]) {
            if (v.is_string()) {
                trans.free_vars.push_back(v.get<std::string>());
            }
        }
    }

    // Parse formula to DNF
    // Pipeline: RewriteBooleans -> RewriteDivision -> RewriteEquality -> Linearizer -> DNF
    if (!trans.formula.empty() && trans.formula != "true") {
        try {
            std::string formula_to_parse = trans.formula;

            // Step 0: RewriteLet (inline let bindings before any other rewriting)
            formula_to_parse = RewriteLet::rewrite(formula_to_parse);

            // Step 1: RewriteBooleans (bare bool var -> integer comparison)
            if (!bool_program_vars.empty()) {
                // Build SSA var set from in_vars/out_vars for boolean program vars
                std::set<std::string> bool_ssa_vars;
                for (const auto& [prog_var, ssa_var] : trans.in_vars) {
                    if (bool_program_vars.count(prog_var)) {
                        bool_ssa_vars.insert(ssa_var);
                    }
                }
                for (const auto& [prog_var, ssa_var] : trans.out_vars) {
                    if (bool_program_vars.count(prog_var)) {
                        bool_ssa_vars.insert(ssa_var);
                    }
                }

                if (!bool_ssa_vars.empty()) {
                    RewriteBooleans bool_rewriter(bool_ssa_vars);
                    formula_to_parse = bool_rewriter.rewriteWithBounds(formula_to_parse);

                    if (VERBOSITY == VerbosityLevel::VERBOSE) {
                        std::cout << "  [RewriteBooleans] Rewrote " << bool_ssa_vars.size()
                                  << " boolean SSA var(s)" << std::endl;
                    }
                }
            }

            // Step 2: RewriteDivision (replace div/mod with linear constraints)
            if (div_rewriter) {
                formula_to_parse = div_rewriter->rewrite(formula_to_parse);
            }

            // Step 3: RewriteEquality (= a b) -> (and (<= a b) (>= a b))
            formula_to_parse = RewriteEquality::rewrite(formula_to_parse);

            // Step 3: FormulaLinearizer (abstract UF, arrays, non-linear mul)
            if (linearizer) {
                LinearizationResult lin_result = linearizer->linearize(formula_to_parse);
                if (lin_result.was_modified) {
                    formula_to_parse = lin_result.linearized_formula;
                }
            }

            trans.dnf = SMTParser::parseFormulaToDNF(formula_to_parse);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to parse formula: " << trans.formula << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return trans;
}

// ============================================================================
// PARSE VARIABLE MAPPING
// ============================================================================

std::map<std::string, std::string> JsonTraceParser::parseVarsMapping(const nlohmann::json& vars_json) {
    std::map<std::string, std::string> mapping;

    for (auto it = vars_json.begin(); it != vars_json.end(); ++it) {
        mapping[it.key()] = it.value().get<std::string>();
    }

    return mapping;
}

// ============================================================================
// VALIDATE JSON STRUCTURE
// ============================================================================

void JsonTraceParser::validateJsonStructure(const nlohmann::json& j) {
    // Check required fields
    if (!j.is_object()) {
        throw std::runtime_error("Root element must be a JSON object");
    }

    // Validate program_vars (optional but should be array if present)
    if (j.contains("program_vars") && !j["program_vars"].is_array()) {
        throw std::runtime_error("'program_vars' must be an array");
    }

    // Validate stem (optional but should be array if present)
    if (j.contains("stem") && !j["stem"].is_array()) {
        throw std::runtime_error("'stem' must be an array");
    }

    // Validate loop (optional but should be array if present)
    if (j.contains("loop") && !j["loop"].is_array()) {
        throw std::runtime_error("'loop' must be an array");
    }

    // Validate transition structure
    auto validate_transition_array = [](const nlohmann::json& arr, const std::string& name) {
        for (const auto& trans : arr) {
            if (!trans.is_object()) {
                throw std::runtime_error(name + " transitions must be objects");
            }
            // Required fields
            if (!trans.contains("formula")) {
                throw std::runtime_error(name + " transition missing 'formula' field");
            }
            if (!trans.contains("in_vars") || !trans["in_vars"].is_object()) {
                throw std::runtime_error(name + " transition 'in_vars' must be an object");
            }
            if (!trans.contains("out_vars") || !trans["out_vars"].is_object()) {
                throw std::runtime_error(name + " transition 'out_vars' must be an object");
            }
        }
    };

    if (j.contains("stem") && j["stem"].is_array()) {
        validate_transition_array(j["stem"], "STEM");
    }

    if (j.contains("loop") && j["loop"].is_array()) {
        validate_transition_array(j["loop"], "LOOP");
    }
}
