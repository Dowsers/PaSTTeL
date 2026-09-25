#ifndef UF_HANDLER_H
#define UF_HANDLER_H

#include <set>
#include <map>
#include "linearization/non_linear_term_handler.h"

/**
 * Handler pour les fonctions non-interprétées (Uninterpreted Functions).
 *
 * Remplace les appels de fonctions comme (keccak256 v_x_1) par des
 * variables fraîches uf__keccak256__0 avec assertion (= uf__keccak256__0 (keccak256 v_x_1)).
 *
 */
class UFHandler : public NonLinearTermHandler {
public:
    /**
     * @param function_names Noms des fonctions non-interprétées à abstraire
     * @param default_sort Sort par défaut si non spécifié ("Int")
     */
    explicit UFHandler(const std::set<std::string>& function_names,
                       const std::string& default_sort = "Int");

    bool canHandle(const std::string& op) const override;
    std::string getPrefix() const override;
    std::string getSort(const std::string& op,
                        const std::vector<std::string>& args) const override;
    std::string getName() const override;

    /**
     * Définit le sort de retour pour une fonction spécifique.
     * Prioritaire sur le default_sort.
     */
    void setFunctionSort(const std::string& function_name, const std::string& sort);

private:
    std::set<std::string> m_function_names;
    std::map<std::string, std::string> m_function_sorts;
    std::string m_default_sort;
};

#endif // UF_HANDLER_H
