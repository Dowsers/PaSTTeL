#ifndef SUPPORTING_INVARIANT_H
#define SUPPORTING_INVARIANT_H

#include <cstdint>
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
};

#endif // SUPPORTING_INVARIANT_H
