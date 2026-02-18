#include "linearization/uf_handler.h"

UFHandler::UFHandler(const std::set<std::string>& function_names,
                     const std::string& default_sort)
    : m_function_names(function_names)
    , m_default_sort(default_sort)
{
}

bool UFHandler::canHandle(const std::string& op) const {
    return m_function_names.count(op) > 0;
}

std::string UFHandler::getPrefix() const {
    return "uf__";
}

std::string UFHandler::getSort(const std::string& op,
                               const std::vector<std::string>& /*args*/) const {
    auto it = m_function_sorts.find(op);
    if (it != m_function_sorts.end()) {
        return it->second;
    }
    return m_default_sort;
}

std::string UFHandler::getName() const {
    return "UFHandler";
}

void UFHandler::setFunctionSort(const std::string& function_name,
                                const std::string& sort) {
    m_function_sorts[function_name] = sort;
}
