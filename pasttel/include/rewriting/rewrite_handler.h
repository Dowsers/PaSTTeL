#ifndef __REWRITE_HANDLER_H
#define __REWRITE_HANDLER_H

#include <string>

#include "linearization/formula_linearizer.h"

class RewriteTermHandler {
public:
    virtual ~RewriteTermHandler() = default;

    virtual bool canHandle(const std::string& op) const = 0;

    virtual std::string rewrite(const std::string& formula) = 0;

    /**
     * Handler name (for logging)
     */
    virtual std::string getName() const = 0;

    virtual std::vector<FunctionAbstraction> getAuxVars() const {
        return {};
    }

    // TODO: add checker for soundness
};

#endif