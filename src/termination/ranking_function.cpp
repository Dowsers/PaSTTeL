#include <sstream>
#include <cstdlib>

#include "termination/ranking_function.h"

std::string RankingFunction::toString(const std::vector<std::string>& vars) const
{
    std::ostringstream oss;
    bool first = true;

    for (const auto& var : vars) {
        auto it = coefficients.find(var);
        if (it->second.isZero()) continue;
        if(!first)
            oss << " + ";
        if (it != coefficients.end()) {
            if (!(it->second.isOne()))
                oss << it->second.toString() << "·";
            oss << var;
            first = false;
        }
    }
    if (!first)
        oss << " + " << constant.toString();
    return oss.str();
}


std::string RankingFunction::toString() const {

    std::ostringstream oss;
    bool first = true;

    for (const auto& [name, value] : coefficients) {
        if (value.isZero()) continue;
        if(!first)
            oss << " + ";
        if (!(value.isOne()))
            oss << value.toString() << "·";
        oss << name;
        first = false;
    }
    if (!first && !constant.isZero())
        oss << " + " << constant.toString();
    return oss.str();

}