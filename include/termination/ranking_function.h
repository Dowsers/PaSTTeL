#ifndef RANKING_FUNCTION_H
#define RANKING_FUNCTION_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "smtsolvers/ModelExtractionUtils.h"

/**
 * @brief Représente une fonction de ranking synthétisée
 *
 * Forme : f(x) = Σ coefficients[var]·var + constant
 * avec décroissance garantie de delta par itération.
 */
struct RankingFunction {
    std::map<std::string, Rational> coefficients;  // var → coefficient
    Rational constant;
    Rational delta;  // Décroissance minimale garantie (δ)

    RankingFunction() : constant(Rational::ZERO()), delta(Rational::ZERO()) {}

    /**
     * @brief Représentation lisible : "3·x + 2·y + 1"
     * @param vars Ordre d'affichage des variables
     */
    std::string toString(const std::vector<std::string>& vars) const;

    std::string toString() const;
};

#endif // RANKING_FUNCTION_H
