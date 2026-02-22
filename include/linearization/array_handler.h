#ifndef ARRAY_HANDLER_H
#define ARRAY_HANDLER_H

#include <set>
#include "linearization/non_linear_term_handler.h"
#include "parser/sexpr_utils.h"

/**
 * Handler pour les operations sur les arrays (select/store).
 *
 * Phase 1 (preprocessing) :
 *   - Simplifie (select (store arr idx val) j) par read-over-write
 *   - Convertit (= arr_new (store arr_old idx val)) en egalites de selects
 *   - Genere les frame conditions pour les indices concrets
 *
 * Phase 2 (linearisation) :
 *   - Remplace (select array index) par des variables fraiches
 *     arr__select__0 avec assertion (= arr__select__0 (select array index))
 *
 */
class ArrayHandler : public NonLinearTermHandler {
public:
    explicit ArrayHandler(const std::string& default_element_sort = "Int");

    // Interface NonLinearTermHandler
    bool canHandle(const std::string& op) const override;
    std::string getPrefix() const override;
    std::string getSort(const std::string& op,
                        const std::vector<std::string>& args) const override;
    std::string getName() const override;

    /**
     * Preprocessing : elimination des stores avant linearisation.
     * 1. Simplifie (select (store ...) ...) par read-over-write
     * 2. Expanse (= arr (store ...)) en egalites de selects + frame conditions
     */
    std::string preprocessFormula(const std::string& formula) const override;

    // Utilitaires statiques

    /** Simplifie recursivement (select (store arr idx val) j) */
    static std::string simplifySelectStore(const std::string& expr);

    /** Convertit les egalites store en egalites de selects */
    static std::string expandStoreEqualities(const std::string& formula);

    /** Evalue un store imbrique a un index donne (read-over-write) */
    static std::string evaluateStoreAtIndex(const std::string& store_expr,
                                             const std::string& index);

    /** Collecte les indices concrets des select/store dans la formule */
    static void collectArrayIndices(const std::string& expr,
                                     std::set<std::string>& indices);

    /** Split S-expression en tokens de premier niveau */
    static std::vector<std::string> splitSExpr(const std::string& expr) {
        return SExprUtils::splitSExpr(expr);
    }

private:
    std::string m_default_element_sort;

    static std::vector<std::string> expandSingleConjunct(
        const std::string& conjunct, const std::set<std::string>& all_indices);

    static std::string trim(const std::string& s) {
        return SExprUtils::trim(s);
    }
};

#endif // ARRAY_HANDLER_H
