#ifndef NLA_HANDLING_H
#define NLA_HANDLING_H

#include <stdexcept>
#include <string>

/**
 * NlaHandling - Stratégie de gestion des termes non-linéaires.
 *
 * Appliquée dans SMTParser::parseAtomicFormula() lorsqu'un terme
 * non-linéaire (ex: (* x y)) est rencontré pendant la conversion
 * en LinearInequality.
 *
 *   OVERAPPROXIMATE  <->  (0 >= 0, tautologie)
 *   UNDERAPPROXIMATE <->  (-1 >= 0, faux)
 *   EXCEPTION        <->  throw TermIsNotAffineException
 */
enum class NlaHandling {
    OVERAPPROXIMATE,   // Remplace l'inégalité non-linéaire par true  (0 >= 0)
    UNDERAPPROXIMATE,  // Remplace l'inégalité non-linéaire par false (-1 >= 0)
    EXCEPTION          // Lance une NlaTermException si terme non-linéaire détecté
};

/**
 * Exception lancée par SMTParser::parseArithExpr() quand un terme
 * non-affine est rencontré (ex: multiplication de deux inconnues).
 *
 */
class NlaTermException : public std::runtime_error {
public:
    explicit NlaTermException(const std::string& msg)
        : std::runtime_error(msg) {}
};

extern NlaHandling NLA_HANDLING;

#endif // NLA_HANDLING_H
