#ifndef __FORMULA_REWRITER_H
#define __FORMULA_REWRITER_H

#include <string>
#include <vector>
#include <memory>


#include "lasso_program.h"
#include "rewriting/rewrite_handler.h"


class FormulaRewriter {
public:
    FormulaRewriter();

    void addHandler(RewriteTermHandler* handler);

    std::string rewrite(const std::string& formula) const;

    void storeAuxVarsToLasso(LassoProgram& lasso);

    bool hasHandlers() const;

private:

    std::vector<RewriteTermHandler*> m_handlers;

    RewriteTermHandler* findHandler(const std::string& op) const;
};



#endif