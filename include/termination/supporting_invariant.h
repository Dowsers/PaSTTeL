#ifndef SUPPORTING_INVARIANT_H
#define SUPPORTING_INVARIANT_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "smtsolvers/ModelExtractionUtils.h"

/**
 * @brief Représente un supporting invariant synthétisé
 *
 * Forme : Σ coefficients[var]·var + constant ⊳ 0
 * où ⊳ est > (strict) ou ≥ (non-strict).
 */
struct SupportingInvariant {
    std::map<std::string, Rational> coefficients;  // var → coefficient
    Rational constant;
    bool is_strict;  // true → >, false → ≥

    SupportingInvariant() : constant(Rational::ZERO()), is_strict(false) {}

    /**
     * @brief Représentation lisible : "2·x - y + 3"
     * @param vars Ordre d'affichage des variables
     */
    std::string toString(const std::vector<std::string>& vars) const;

    /**
     * @brief Same as toString(vars), but each variable is rendered via
     * display_name(var) instead of printed as-is -- lookup in `coefficients`
     * still uses the raw `var` from `vars`, only what gets printed changes.
     * Lets a caller with extra context (e.g. LassoProgram::prettyVarName())
     * show "A[i]" instead of a raw promoted-array-cell variable name.
     */
    std::string toString(const std::vector<std::string>& vars,
                          const std::function<std::string(const std::string&)>& display_name) const;
};

#endif // SUPPORTING_INVARIANT_H
