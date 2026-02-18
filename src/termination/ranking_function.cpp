#include <sstream>
#include <cmath>

#include "termination/ranking_function.h"

std::string RankingFunction::toString(const std::vector<std::string>& vars) const
{
    std::ostringstream oss;
    bool first = true;

    for (const auto& var : vars) {
        auto it = coefficients.find(var);
        if (it != coefficients.end() && std::abs(it->second) > 1e-9) {
            if (!first && it->second > 0) {
                oss << " + ";
            } else if (it->second < 0) {
                oss << " - ";
            }

            double abs_coef = std::abs(it->second);
            if (std::abs(abs_coef - 1.0) > 1e-9) {
                oss << abs_coef << "·";
            }
            oss << var;
            first = false;
        }
    }

    if (std::abs(constant) > 1e-9) {
        if (!first && constant > 0) {
            oss << " + ";
        } else if (constant < 0) {
            oss << " - ";
        }
        oss << std::abs(constant);
    } else if (first) {
        oss << "0";
    }

    return oss.str();
}
