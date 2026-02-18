#ifndef RANKING_FUNCTION_H
#define RANKING_FUNCTION_H

#include <map>
#include <string>
#include <vector>

/**
 * @brief Représente une fonction de ranking synthétisée
 *
 * Forme : f(x) = Σ coefficients[var]·var + constant
 * avec décroissance garantie de delta par itération.
 */
struct RankingFunction {
    std::map<std::string, double> coefficients;  // var → coefficient
    double constant;
    double delta;  // Décroissance minimale garantie (δ)

    RankingFunction() : constant(0.0), delta(0.0) {}

    /**
     * @brief Représentation lisible : "3·x + 2·y + 1"
     * @param vars Ordre d'affichage des variables
     */
    std::string toString(const std::vector<std::string>& vars) const;
};

#endif // RANKING_FUNCTION_H
