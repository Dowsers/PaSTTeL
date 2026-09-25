#ifndef REWRITE_STRICT_INEQUALITIES_H
#define REWRITE_STRICT_INEQUALITIES_H

#include <string>

#include "rewrite_handler.h"

/**
 * RewriteStrictInequalities - Convert strict integer inequalities to non-strict.
 *
 *   (< a b)  -->  (<= a (- b 1))       i.e. a < b  ⟺  a ≤ b-1  (integers)
 *   (> a b)  -->  (>= a (+ b 1))       i.e. a > b  ⟺  a ≥ b+1  (integers)
 *
 * Only sound for integer-valued variables.
 */
class RewriteStrictInequalities : public RewriteTermHandler {
public:
    bool canHandle(const std::string& op) const override;
    std::string rewrite(const std::string& formula) override;
    std::string getName() const override;
};

#endif // REWRITE_STRICT_INEQUALITIES_H
