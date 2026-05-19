#include <sstream>
#include <cstdlib>

#include "termination/supporting_invariant.h"

std::string SupportingInvariant::toString(const std::vector<std::string>& vars) const
{
    std::ostringstream oss;
    bool first = true;

    for (const auto& var : vars) {
        auto it = coefficients.find(var);
        if (it != coefficients.end() && !it->second.isZero()) {
            Rational abs_coef = it->second.abs();
            bool negative = (it->second.numerator() < BigInt(0));
            if (!first && !negative) {
                oss << " + ";
            } else if (negative) {
                oss << " - ";
            }
            if (!abs_coef.isOne()) {
                oss << abs_coef.toString() << "·";
            }
            oss << var;
            first = false;
        }
    }

    if (!constant.isZero()) {
        bool neg = (constant.numerator() < BigInt(0));
        if (!first && !neg) {
            oss << " + ";
        } else if (neg) {
            oss << " - ";
        }
        oss << constant.abs().toString();
    } else if (first) {
        oss << "0";
    }

    return oss.str();
}
