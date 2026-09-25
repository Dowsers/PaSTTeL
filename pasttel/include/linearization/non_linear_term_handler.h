#ifndef NON_LINEAR_TERM_HANDLER_H
#define NON_LINEAR_TERM_HANDLER_H

#include <string>
#include <vector>

/**
 * Interface pour les handlers de termes non-linéaires.
 *
 * Chaque handler sait détecter et abstraire un type de terme non-linéaire
 * (fonctions non-interprétées, select/store sur arrays, opérations sur strings, etc.)
 *
 * Le FormulaLinearizer interroge ses handlers lors du parcours récursif :
 * si un handler reconnaît l'opérateur, le terme est remplacé par une variable fraîche.
 */
class NonLinearTermHandler {
public:
    virtual ~NonLinearTermHandler() = default;

    /**
     * Détermine si ce handler gère l'opérateur donné.
     * @param op Nom de l'opérateur (ex: "keccak256", "select", "str.len")
     * @return true si ce handler veut abstraire ce terme
     */
    virtual bool canHandle(const std::string& op) const = 0;

    /**
     * Préfixe pour les noms de variables fraîches.
     * Ex: "uf__" pour les UF, "arr__" pour les arrays
     */
    virtual std::string getPrefix() const = 0;

    /**
     * Détermine le sort (type SMT) de la variable fraîche.
     * @param op Nom de l'opérateur
     * @param args Arguments du terme (déjà linéarisés)
     * @return Sort SMT-LIB (ex: "Int", "Bool", "(Array Int Int)")
     */
    virtual std::string getSort(const std::string& op,
                                const std::vector<std::string>& args) const = 0;

    /**
     * Nom du handler (pour le logging).
     */
    virtual std::string getName() const = 0;

    /**
     * Preprocessing de la formule avant la passe de linearisation.
     * Permet a chaque handler de transformer la formule globalement
     * (ex: expansion des stores en selects pour les arrays).
     * Par defaut, retourne la formule inchangee.
     */
    virtual std::string preprocessFormula(const std::string& formula) const {
        return formula;
    }
};

#endif // NON_LINEAR_TERM_HANDLER_H
