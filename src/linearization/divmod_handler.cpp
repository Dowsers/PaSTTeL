#include "linearization/divmod_handler.h"

bool DivModHandler::canHandle(const std::string& op) const {
    return op == "div" || op == "mod";
}

std::string DivModHandler::getPrefix() const {
    return "arith__";
}

std::string DivModHandler::getSort(const std::string& /*op*/,
                                   const std::vector<std::string>& /*args*/) const {
    // div et mod retournent toujours des entiers en SMT-LIB
    return "Int";
}

std::string DivModHandler::getName() const {
    return "DivModHandler";
}
