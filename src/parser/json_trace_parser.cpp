#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <memory>
#include <regex>

#include "parser/json_trace_parser.h"
#include "linearization/formula_linearizer.h"
#include "linearization/uf_handler.h"
#include "linearization/array_handler.h"
#include "rewriting/rewrite_let.h"
#include "rewriting/rewrite_division_modulo.h"
#include "rewriting/rewrite_equality.h"
#include "rewriting/rewrite_booleans.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

void JsonTraceParser::removeArrayVarsFromProgramVars(
                    std::vector<std::string>& program_vars,
                    const std::map<std::string, std::string>& var_sorts) {
    auto it = program_vars.begin();
    while (it != program_vars.end()) {
        auto sort_it = var_sorts.find(*it);
        if (sort_it != var_sorts.end() &&
            sort_it->second.find("Array") != std::string::npos) {
            if (VERBOSITY == VerbosityLevel::VERBOSE)
                std::cout << "  Filtered Array var from program_vars: "
                            << *it << " (" << sort_it->second << ")" << std::endl;
            it = program_vars.erase(it);
        } else {
            ++it;
        }
    }
}

bool checkDivModOp(const std::string formula) {
    if (formula.find("(div ") != std::string::npos ||
            formula.find("(mod ") != std::string::npos){
        if (VERBOSITY == VerbosityLevel::VERBOSE)
            std::cout << "Detected div/mod operations in formulas" << std::endl;
        return true;
    }
    return false;
}

bool checkArrayVars(const std::map<std::string, std::string>& var_sorts) {
    for (const auto& [var_name, var_sort] : var_sorts) {
        if (var_sort.find("Array") != std::string::npos){
            if (VERBOSITY == VerbosityLevel::VERBOSE)
                std::cout << "Detected Array vars in formulas" << std::endl;
            return true;
        }
    }
    return false;
}

std::set<std::string> extractBoolVars(const std::map<std::string, std::string>& var_sorts) {
    std::set<std::string> bool_vars;
    for (const auto& [var_name, var_sort] : var_sorts) {
        if (var_sort == "Bool")
            bool_vars.insert(var_name);
    }
    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "Detected booleans in program variables: "<< bool_vars.size() << std::endl;
    return bool_vars;
}


void setLoopVariables(LassoProgram& lasso){
    lasso.loop_vars.clear();
    for (const auto& var : lasso.program_vars) {
        bool in_loop_in  = lasso.loop.var_to_ssa_in.count(var) > 0;
        bool in_loop_out = lasso.loop.var_to_ssa_out.count(var) > 0;
        if (in_loop_in && in_loop_out) {
            lasso.loop_vars.push_back(var);
        }
    }
}

// void removeUnusedVariables(LassoProgram& lasso){
//     std::set<std::string> used_vars;
//     for (const auto& poly : lasso.stem.polyhedra) {
//         for (const auto& ineq : poly) {
//             for (const auto& [var, _] : ineq.coefficients)
//                 used_vars.insert(var);
//             for (const auto& [var, _] : ineq.constant.coefficients)
//                 used_vars.insert(var);
//         }
//     }
//     for (const auto& poly : lasso.loop.polyhedra) {
//         for (const auto& ineq : poly) {
//             for (const auto& [var, _] : ineq.coefficients)
//                 used_vars.insert(var);
//             for (const auto& [var, _] : ineq.constant.coefficients)
//                 used_vars.insert(var);
//         }
//     }

//     // Filter var_to_ssa_in and var_to_ssa_out to keep only used variables
//     auto filterVars = [&](std::map<std::string, std::string>& var_map, std::set<std::string>& used_vars) {
//         std::map<std::string, std::string> filtered;
//         for (const auto& [var_prog, ssa] : var_map) {
//             if (used_vars.count(ssa) > 0) {
//                 filtered[var_prog] = ssa;
//             }
//         }
//         var_map = std::move(filtered);
//     };

//     filterVars(lasso.stem.var_to_ssa_in, used_vars);
//     filterVars(lasso.stem.var_to_ssa_out, used_vars);
//     filterVars(lasso.loop.var_to_ssa_in, used_vars);
//     filterVars(lasso.loop.var_to_ssa_out, used_vars);

//     // Update program_vars to keep only those that are still mapped
//     // std::set<std::string> mapped_program_vars;
//     // for (const auto& [var_prog, ssa] : lasso.stem.var_to_ssa_in)
//     //     mapped_program_vars.insert(var_prog);
//     // for (const auto& [var_prog, ssa] : lasso.stem.var_to_ssa_out)
//     //     mapped_program_vars.insert(var_prog);
//     // for (const auto& [var_prog, ssa] : lasso.loop.var_to_ssa_in)
//     //     mapped_program_vars.insert(var_prog);
//     // for (const auto& [var_prog, ssa] : lasso.loop.var_to_ssa_out)
//     //     mapped_program_vars.insert(var_prog);

//     // lasso.program_vars.clear();
//     // for (const auto& var : mapped_program
// }

void connectStemToLoop(LassoProgram& lasso) {
    if (lasso.stem.isTrue() || lasso.loop.isTrue()) {
        if (VERBOSITY != VerbosityLevel::QUIET) {
            std::cout << "\n=== No STEM or no LOOP to connect ===" << std::endl;
            std::cout << "STEM isTrue: " << lasso.stem.isTrue() << ", LOOP isTrue: " << lasso.loop.isTrue() << std::endl;
        }
        return;
    }

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
        // Substitue un nom de variable SSA selon la table.
        // Pour les identifiants quotés SMT-LIB2 |...[idx]...|, seul l'index
        // interne peut être substitué ; le nom complet reste entre pipes.
        auto substituteVar = [&](const std::string& var_ssa) -> std::string {
            // Identifiant quoté |...| : substituer l'index interne si besoin
            if (var_ssa.size() >= 2 && var_ssa.front() == '|' && var_ssa.back() == '|') {
                // Cherche [idx] à l'intérieur des pipes
                size_t bracket = var_ssa.find('[');
                if (bracket != std::string::npos) {
                    size_t close = var_ssa.find(']', bracket);
                    std::string idx = var_ssa.substr(bracket + 1, close - bracket - 1);
                    auto it_idx = substitution.find(idx);
                    if (it_idx != substitution.end()) {
                        return var_ssa.substr(0, bracket + 1)
                            + it_idx->second
                            + var_ssa.substr(close);
                    }
                }
                return var_ssa;
            }
            // Variable scalaire ou notation arr[idx] sans pipes
            size_t bracket = var_ssa.find('[');
            if (bracket != std::string::npos) {
                std::string arr = var_ssa.substr(0, bracket);
                std::string idx = var_ssa.substr(bracket + 1, var_ssa.find(']') - bracket - 1);
                auto it_arr = substitution.find(arr);
                auto it_idx = substitution.find(idx);
                if (it_arr != substitution.end()) arr = it_arr->second;
                if (it_idx != substitution.end()) idx = it_idx->second;
                return arr + "[" + idx + "]";
            }
            auto it = substitution.find(var_ssa);
            return (it != substitution.end()) ? it->second : var_ssa;
        };

        for (auto& poly : lasso.loop.polyhedra) {
            for (auto& ineq : poly) {
                // Substitute in coefficients
                std::map<std::string, AffineTerm> new_coeffs;
                for (const auto& [var_ssa, coef] : ineq.coefficients)
                    new_coeffs[substituteVar(var_ssa)] = coef;
                ineq.coefficients = new_coeffs;

                // Substitute in constant.coefficients
                std::map<std::string, double> new_const_coeffs;
                for (const auto& [var_ssa, val] : ineq.constant.coefficients)
                    new_const_coeffs[substituteVar(var_ssa)] = val;
                ineq.constant.coefficients = new_const_coeffs;
            }
        }

        // Update var_to_ssa_in of loop (uses substituteVar to handle quoted identifiers)
        for (auto& [var_prog, ssa_in] : lasso.loop.var_to_ssa_in) {
            ssa_in = substituteVar(ssa_in);
        }

        // Update var_to_ssa_out of loop (uses substituteVar to handle quoted identifiers)
        for (auto& [var_prog, ssa_out] : lasso.loop.var_to_ssa_out) {
            ssa_out = substituteVar(ssa_out);
        }
    }
}


void storeAbstractFunctionsToLasso(LassoProgram& lasso,
        const std::vector<UltimateTransitionLine>& stem_lines,
        const std::vector<UltimateTransitionLine>& loop_lines) {

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

void addFreeAuxVariables(LassoProgram& lasso,
                const std::vector<UltimateTransitionLine>& stem_lines,
                const std::vector<UltimateTransitionLine>& loop_lines) {
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


void initializeOptionsForAnalysis(LassoProgram& lasso) {
    // Si le programme contient des variables de type Int, activer integer_mode for GNTA
    for (const auto& [var_name, var_sort] : lasso.var_sorts) {
        if (var_sort == "Int") {
            lasso.integer_mode = true;
            break;
        }
    }
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "Integer mode: " << (lasso.integer_mode ? "ON" : "OFF") << std::endl;
    }
}

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

    // 4. Extract constants (if present)
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

    // 5. Extract array variables (if present)
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

    // 6. Extract var_types (if present)
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
    if (VERBOSITY == VerbosityLevel::VERBOSE)
        std::cout << "Detected booleans in program variables: "<< bool_vars.size() << std::endl;

    // 7. Filter Array vars from program_vars (no linear coefficient possible).
    // Bool vars are KEPT — their 0/1 bounds are injected by rewrite.
    JsonTraceParser::removeArrayVarsFromProgramVars(lasso.program_vars, lasso.var_sorts);

    // 8. Extract uninterpreted functions (if present)
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

    // 9. Extract axioms (if present)
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

    // 10. Check for div/mod operations in formulas
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

    // 11. Create RewriteDivisionMod for div/mod rewriting
    // This replaces (div x y) and (mod x y) with auxiliary variables
    // and conjoins equivalent linear constraints directly into the formula.
    FormulaRewriter * rewriter = new FormulaRewriter();
    rewriter->addHandler(new RewriteLet());

    if (has_divmod) {
        rewriter->addHandler(new RewriteDivisionMod());
    }
    if (!bool_vars.empty()) {
        rewriter->addHandler(new RewriteBooleans(bool_vars));
    }
    // always at the end: rewrite equalities after all other transformations
    rewriter->addHandler(new RewriteEquality());


    // 12. Create FormulaLinearizer with appropriate handlers
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
            stem_lines.push_back(parseTransition(trans_json, linearizer.get(), rewriter));
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\nStem transitions: " << stem_lines.size() << std::endl;
        for(const auto& line : stem_lines) {
            std::cout<< "\tOriginal Formula: "<< line.formula << std::endl;
            std::cout << "\tDNF:\n" << line.dnf.toString() << std::endl;
        }
    }

    // 5. Parse LOOP transitions
    std::vector<UltimateTransitionLine> loop_lines;
    if (j.contains("loop") && j["loop"].is_array()) {
        for (const auto& trans_json : j["loop"]) {
            loop_lines.push_back(parseTransition(trans_json, linearizer.get(), rewriter));
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\nLoop transitions: " << loop_lines.size() << std::endl;
        for(const auto& line : loop_lines) {
            std::cout<< "\tOriginal Formula: "<< line.formula << std::endl;
            std::cout << "\tDNF:\n" << line.dnf.toString() << std::endl;
        }
    }

    // 6. Build transitions using LinearTransition (composition logic)
    if (!stem_lines.empty()) {
        lasso.stem = LinearTransition::buildFromLines(stem_lines);
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "STEM built with " << lasso.stem.var_to_ssa_in.size() << " input vars" << std::endl;
        }
    }

    if (!loop_lines.empty()) {
        lasso.loop = LinearTransition::buildFromLines(loop_lines);
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "LOOP built with " << lasso.loop.var_to_ssa_out.size() << " output vars" << std::endl;
        }
    }

    // 7. Connect STEM->LOOP
    connectStemToLoop(lasso);

    // 7b0. Calculer loop_vars AVANT ensureMapping :
    //      intersection loop.var_to_ssa_in ∩ loop.var_to_ssa_out depuis le JSON original.
    //      template variables = loop.getOutVars() ∩ loop.getInVars().
    //      Après ensureMapping, tous les program_vars sont dans les deux maps (via fresh vars),
    //      donc l'intersection ne peut pas être faite après.
    setLoopVariables(lasso);

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

    // 8a. Store RewriteDivisionMod auxiliary variables FIRST
    // These must be declared before linearizer abstractions because the linearizer's
    // original_call may reference div_aux/mod_aux vars (e.g., "(= nlmul__0 (* div_aux_0 y))").

    if (rewriter)
        rewriter->storeAuxVarsToLasso(lasso);

    // 8b. Store linearizer function abstractions with composition substitutions applied
    if(linearizer)
        linearizer->storeAbstractionsToLasso(lasso);

    storeAbstractFunctionsToLasso(lasso, stem_lines, loop_lines);

    // 8c. Register free_vars (auxiliary variables) from all transitions as function_abstractions (Int, no assertion).
    // These are SSA variables present in the formula but not in in_vars/out_vars —
    // They must be declared in the solver but carry no ranking-function coefficient.
    addFreeAuxVariables(lasso, stem_lines, loop_lines);

    initializeOptionsForAnalysis(lasso);

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n=== LassoProgram constructed successfully ===" << std::endl;
        std::cout << "Loop vars (in ∩ out): ";
        for (const auto& v : lasso.loop_vars) std::cout << v << " ";
        std::cout << std::endl;
    }

    return lasso;
}


// ============================================================================
// MAIN PARSING SINGLE STRING SMT TRANSITION
// ============================================================================

void JsonTraceParser::convertLassoStringToLassoProgram(
    const std::string& stem_formula,
    const std::string& loop_formula,
    LassoProgram& lasso) {

    std::vector<UltimateTransitionLine> stem_lines;
    stem_lines.push_back(convertSMTFormula2ToLinearInequalities(stem_formula, lasso));
    stem_lines[0].in_vars = lasso.stem.var_to_ssa_in;
    stem_lines[0].out_vars = lasso.stem.var_to_ssa_out;
    std::vector<UltimateTransitionLine> loop_lines;
    loop_lines.push_back(convertSMTFormula2ToLinearInequalities(loop_formula, lasso));
    loop_lines[0].in_vars = lasso.loop.var_to_ssa_in;
    loop_lines[0].out_vars = lasso.loop.var_to_ssa_out;

    lasso.stem = LinearTransition::buildFromLines(stem_lines);
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "STEM built with " << lasso.stem.var_to_ssa_in.size() << " input vars" << std::endl;
    }
    
    lasso.loop = LinearTransition::buildFromLines(loop_lines);
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "LOOP built with " << lasso.loop.var_to_ssa_out.size() << " output vars" << std::endl;
    }

    connectStemToLoop(lasso);

    setLoopVariables(lasso);

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n=== After connecting STEM to LOOP ===" << std::endl;
        std::cout << "STEM input vars: " << lasso.stem.var_to_ssa_in.size() << std::endl;
        std::cout << "STEM output vars: " << lasso.stem.var_to_ssa_out.size() << std::endl;
        std::cout << "LOOP input vars: " << lasso.loop.var_to_ssa_in.size() << std::endl;
        std::cout << "LOOP output vars: " << lasso.loop.var_to_ssa_out.size() << std::endl;
        std::cout << "Loop vars (in ∩ out) (size " << lasso.loop_vars.size() << "): ";
        for (const auto& v : lasso.loop_vars) std::cout << v << " ";
        std::cout << std::endl;
    }

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

    storeAbstractFunctionsToLasso(lasso, stem_lines, loop_lines);

    addFreeAuxVariables(lasso, stem_lines, loop_lines);

    initializeOptionsForAnalysis(lasso);
}

UltimateTransitionLine JsonTraceParser::convertSMTFormula2ToLinearInequalities(
    const std::string& formula,
    LassoProgram& lasso) {
    std::vector<LinearInequality> inequalities;
    std::unique_ptr<FormulaLinearizer> linearizer = NULL;
    FormulaRewriter * rewriter = NULL;
    
    if (formula == "false") {
        throw std::runtime_error("Formula is false. It's not a valid lasso program");
    }

    if (formula.empty()) {
        throw std::runtime_error("Formula is empty. It's not a valid lasso program");
    }

    std::set<std::string> bool_vars = extractBoolVars(lasso.var_sorts);
    bool has_arrays = checkArrayVars(lasso.var_sorts);
    bool has_divmod = checkDivModOp(formula);
    bool has_let = formula.find("(let ") != std::string::npos;
    bool has_equality = formula.find("(= ") != std::string::npos;

    JsonTraceParser::removeArrayVarsFromProgramVars(lasso.program_vars, lasso.var_sorts);
    
    bool needs_rewriter = !bool_vars.empty() || has_divmod || has_let || has_equality;
    bool needs_linearizer = !lasso.functions.empty() || has_arrays;

    if (needs_linearizer){
        linearizer = std::make_unique<FormulaLinearizer>();
        // Add ArrayHandler for array select operations
        if (has_arrays) {
            auto array_handler = std::make_unique<ArrayHandler>();
            linearizer->addHandler(std::move(array_handler));

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaLinearizer: ArrayHandler added for select linearization" << std::endl;
            }
        }
    }
    if (needs_rewriter) {
        rewriter = new FormulaRewriter();
        // Add LetHandler for let inlining
        if (has_let) {
            rewriter->addHandler(new RewriteLet());

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaRewriter: RewriteLetHandler added for let inlining" << std::endl;
            }
        }
        // Add BooleanHandler for boolean variable rewriting
        if (!bool_vars.empty()) {
            rewriter->addHandler(new RewriteBooleans(bool_vars));

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaRewriter: RewriteBooleans added for boolean variable linearization" << std::endl;
            }
        }
        // Add DivisionHandler for div/mod rewriting
        if (has_divmod) {
            rewriter->addHandler(new RewriteDivisionMod());

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaRewriter: RewriteDivisionMod added for div/mod linearization" << std::endl;
            }
        }
        // Add EqualityHandler for equality rewriting
        // Always at the end: rewrite equalities after all other transformations 
        if (has_equality) {
            rewriter->addHandler(new RewriteEquality());

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "FormulaRewriter: RewriteEqualityHandler added for equality linearization" << std::endl;
            }
        }
    }


    UltimateTransitionLine trans;

    trans.formula = formula;
    // trans.free_vars = lasso.aux_vars;

    if (!trans.formula.empty() && trans.formula != "true") {
        try {
            std::string formula_to_parse = trans.formula;
            if(rewriter)
                formula_to_parse = rewriter->rewrite(formula_to_parse);
            if (linearizer) {
                LinearizationResult lin_result = linearizer->linearize(formula_to_parse);
                if (lin_result.was_modified) {
                    formula_to_parse = lin_result.linearized_formula;
                }
            }

            std::cout<<"---- Formula before DNF :"<< formula_to_parse << std::endl;

            trans.dnf = SMTParser::parseFormulaToDNF(formula_to_parse);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to parse formula: " << trans.formula << std::endl;
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    
    if(rewriter)
        rewriter->storeAuxVarsToLasso(lasso);
    if (linearizer)
        linearizer->storeAbstractionsToLasso(lasso);

    return trans;
}

// ============================================================================
// PARSE SINGLE TRANSITION
// ============================================================================

UltimateTransitionLine JsonTraceParser::parseTransition(
    const nlohmann::json& trans_json,
    FormulaLinearizer* linearizer,
    FormulaRewriter* rewriter) {
    UltimateTransitionLine trans;

    // Extract source label
    if (trans_json.contains("source")) {
        trans.label = trans_json["source"].get<std::string>();
    }

    // Extract formula
    if (trans_json.contains("formula")) {
        trans.formula = trans_json["formula"].get<std::string>();
        if (trans.formula == "false") {
            throw std::runtime_error("Formula is false. It's not a valid lasso program");
        }
        if (trans.formula.empty()) {
            throw std::runtime_error("Formula is empty. It's not a valid lasso program");
        }
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
    // Pipeline: RewriteBooleans -> RewriteDivisionMod -> RewriteEquality -> Linearizer -> DNF
    if (!trans.formula.empty() && trans.formula != "true") {
        try {
            std::string formula_to_parse = trans.formula;

            if(VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "\nOriginal formula: " << formula_to_parse << std::endl;
            }
            if(rewriter)
                formula_to_parse = rewriter->rewrite(formula_to_parse);

            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "After rewriting: " << formula_to_parse << std::endl;
            }
            // // Step 0: RewriteLet (inline let bindings before any other rewriting)
            // RewriteLet let_rewriter = RewriteLet();
            // formula_to_parse = let_rewriter.rewrite(formula_to_parse);

            // // Step 1: RewriteBooleans (bare bool var -> integer comparison)
            // if (!bool_program_vars.empty()) {
            //     // Build SSA var set from in_vars/out_vars for boolean program vars
            //     std::set<std::string> bool_ssa_vars;
            //     for (const auto& [prog_var, ssa_var] : trans.in_vars) {
            //         if (bool_program_vars.count(prog_var)) {
            //             bool_ssa_vars.insert(ssa_var);
            //         }
            //     }
            //     for (const auto& [prog_var, ssa_var] : trans.out_vars) {
            //         if (bool_program_vars.count(prog_var)) {
            //             bool_ssa_vars.insert(ssa_var);
            //         }
            //     }

            //     if (!bool_ssa_vars.empty()) {
            //         RewriteBooleans bool_rewriter(bool_ssa_vars);
            //         formula_to_parse = bool_rewriter.rewrite(formula_to_parse);

            //         if (VERBOSITY == VerbosityLevel::VERBOSE) {
            //             std::cout << "  [RewriteBooleans] Rewrote " << bool_ssa_vars.size()
            //                       << " boolean SSA var(s)" << std::endl;
            //         }
            //     }
            // }

            // Step 2: FormulaLinearizer (abstract UF, arrays, non-linear mul)
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
