#ifndef DIVMOD_HANDLER_H
#define DIVMOD_HANDLER_H

#include "linearization/non_linear_term_handler.h"

/**
 * Handler pour les operations de division et modulo entier (div, mod).
 *
 * Ces operations ne sont pas lineaires et ne peuvent pas etre representees
 * directement par un AffineTerm. Ce handler les remplace par des variables
 * fraiches pour permettre l'analyse de terminaison.
 *
 * Exemple :
 *   (div v_x_1 v_y_2)  ->  arith__div__0
 *   avec assertion : (= arith__div__0 (div v_x_1 v_y_2))
 *
 *   (mod v_x_1 2)  ->  arith__mod__1
 *   avec assertion : (= arith__mod__1 (mod v_x_1 2))
 *
 * Note: Meme si un operande est constant (ex: (div x 2)), le resultat
 * n'est pas lineaire car la division entiere n'est pas une multiplication
 * par l'inverse (contrairement a la division reelle).
 */
class DivModHandler : public NonLinearTermHandler {
public:
    DivModHandler() = default;

    bool canHandle(const std::string& op) const override;
    std::string getPrefix() const override;
    std::string getSort(const std::string& op,
                        const std::vector<std::string>& args) const override;
    std::string getName() const override;
};

#endif // DIVMOD_HANDLER_H
