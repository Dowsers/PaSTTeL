#include <iostream>

#include "rewriting/formula_rewriter.h"
#include "utiles.h"

FormulaRewriter::FormulaRewriter() {
}

void FormulaRewriter::addHandler(RewriteTermHandler* handler) {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  [FormulaRewriter] Handler added: " << handler->getName() << std::endl;
    }
    m_handlers.push_back(handler);
}

bool FormulaRewriter::hasHandlers() const {
    return !m_handlers.empty();
}

void FormulaRewriter::storeAuxVarsToLasso(LassoProgram& lasso) {
    for (const auto& handler : m_handlers) {
        const auto& aux_vars = handler->getAuxVars();
        for(auto& abs : aux_vars)
            lasso.function_abstractions.push_back(abs);
        
        if (VERBOSITY == VerbosityLevel::VERBOSE && !aux_vars.empty()) {
            std::cout << "  ["<< handler->getName() <<"] Stored " << aux_vars.size()
                    << " to LassoProgram abstractions" << std::endl;
            for (const auto& abs : aux_vars) {
                std::cout << "  " << abs.fresh_var << " (" << abs.sort << ")" << std::endl;
            }
        }
    }
}

RewriteTermHandler* FormulaRewriter::findHandler(const std::string& op) const {
    for (const auto& handler : m_handlers) {
        if (handler->canHandle(op)) {
            return handler;
        }
    }
    return nullptr;
}

std::string FormulaRewriter::rewrite(const std::string& formula) const {
    std::string new_formula = formula;
    if (formula.empty() || formula == "true" || m_handlers.empty()) {
        return formula;
    }

    for(const auto& handler : m_handlers) {
        new_formula = handler->rewrite(new_formula);
        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "  [FormulaRewriter] After " << handler->getName() << ": "
                    << new_formula << std::endl;
        }
    }
    return new_formula;
}