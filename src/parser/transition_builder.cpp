#include <iostream>

#include "parser/transition_builder.h"
#include "parser/json_trace_parser.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

LinearTransition TransitionBuilder::buildFromLines(
    const std::vector<UltimateTransitionLine>& lines
) {
    if (lines.empty()) {
        return LinearTransition();
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n--- Building transition from " << lines.size()
                  << " JSON transitions ---" << std::endl;
    }

    LinearTransition accumulated;
    bool first = true;

    for (size_t i = 0; i < lines.size(); ++i) {
        const UltimateTransitionLine& line = lines[i];

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "\nProcessing transition " << (i + 1) << "/" << lines.size()
                      << " (label: " << line.label << ")" << std::endl;
        }

        LinearTransition current_trans;

        current_trans.var_to_ssa_in = line.in_vars;
        current_trans.var_to_ssa_out = line.out_vars;

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "  InVars: ";
            for (const auto& [var, ssa] : line.in_vars) {
                std::cout << var << "=" << ssa << " ";
            }
            std::cout << std::endl;

            std::cout << "  OutVars: ";
            for (const auto& [var, ssa] : line.out_vars) {
                std::cout << var << "=" << ssa << " ";
            }
            std::cout << std::endl;
        }

        // Add polyhedra from the DNF
        if (!line.dnf.polyhedra.empty()) {
            for (const auto& poly : line.dnf.polyhedra) {
                current_trans.addPolyhedron(poly);
            }
        } else if (line.formula == "true" || line.formula.empty()) {
            current_trans.addPolyhedron({});
        } else {
            std::cerr << "  Warning: Formula not parsed: " << line.formula << std::endl;
        }

        // Sequential composition
        if (first) {
            accumulated = current_trans;
            first = false;
        } else {
            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "  Composing with accumulated transition..." << std::endl;
            }
            accumulated = accumulated.compose(current_trans);
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n--- Transition building complete ---" << std::endl;
    }
    return accumulated;
}