#include <sstream>
#include <cstdlib>

#include "termination/supporting_invariant.h"

std::string SupportingInvariant::toString(const std::vector<std::string>& vars) const
{
    std::ostringstream oss;
    bool first = true;

    for (const auto& var : vars) {
        auto it = coefficients.find(var);
        if (it != coefficients.end() && it->second != 0) {
            if (!first && it->second > 0) {
                oss << " + ";
            } else if (it->second < 0) {
                oss << " - ";
            }

            int64_t abs_coef = std::abs(it->second);
            if (abs_coef != 1) {
                oss << abs_coef << "·";
            }
            oss << var;
            first = false;
        }
    }

    if (constant != 0) {
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
