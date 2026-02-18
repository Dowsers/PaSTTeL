#include <sstream>
#include <cmath>
#include <vector>

#include "../include/affine_term.h"
#include "utiles.h"

// Constructeur par défaut - crée le terme 0
AffineTerm::AffineTerm() : constant(0.0) {}

// Constructeur pour une constante pure
AffineTerm::AffineTerm(double c) : constant(c) {}

// Constructeur de copie
AffineTerm::AffineTerm(const AffineTerm& other)
    : coefficients(other.coefficients), constant(other.constant) {}

// Vérifie si le terme est zéro (constante et tous coefficients nuls)
bool AffineTerm::isZero() const {
    if (std::abs(constant) > 1e-10) return false;
    for (const auto& [param, coef] : coefficients) {
        if (std::abs(coef) > 1e-10) return false;
    }
    return true;
}

// Vérifie si c'est une constante pure (pas de variables)
bool AffineTerm::isConstant() const {
    return coefficients.empty();
}

// Addition de deux termes affines
AffineTerm AffineTerm::operator+(const AffineTerm& other) const {
    AffineTerm result(*this);
    result += other;
    return result;
}

// Soustraction de deux termes affines
AffineTerm AffineTerm::operator-(const AffineTerm& other) const {
    AffineTerm result(*this);
    AffineTerm neg = other;
    neg.negate();
    result += neg;
    return result;
}

// Multiplication par un scalaire
AffineTerm AffineTerm::operator*(double scalar) const {
    AffineTerm result(*this);
    result *= scalar;
    return result;
}

// Addition en place
AffineTerm& AffineTerm::operator+=(const AffineTerm& other) {
    // Additionne la constante
    constant += other.constant;
    
    // Additionne les coefficients
    for (const auto& [param, coef] : other.coefficients) {
        coefficients[param] += coef;
        // Supprime les coefficients devenus nuls
        if (std::abs(coefficients[param]) < 1e-10) {
            coefficients.erase(param);
        }
    }
    return *this;
}

// Multiplication en place par un scalaire
AffineTerm& AffineTerm::operator*=(double scalar) {
    constant *= scalar;
    for (auto& [param, coef] : coefficients) {
        coef *= scalar;
    }
    // Nettoyer les coefficients devenus très petits
    for (auto it = coefficients.begin(); it != coefficients.end();) {
        if (std::abs(it->second) < 1e-10) {
            it = coefficients.erase(it);
        } else {
            ++it;
        }
    }
    return *this;
}

// Négation (multiplication par -1)
void AffineTerm::negate() {
    *this *= -1.0;
}

// Conversion en string lisible
std::string AffineTerm::toString() const {
    if (isZero()) return "0";
    
    std::ostringstream oss;
    bool first = true;
    
    // Affiche les termes avec variables
    for (const auto& [param, coef] : coefficients) {
        if (std::abs(coef) < 1e-10) continue;
        
        if (!first) {
            oss << (coef > 0 ? " + " : " - ");
        } else if (coef < 0) {
            oss << "-";
        }
        
        double abs_coef = std::abs(coef);
        if (std::abs(abs_coef - 1.0) > 1e-10) {
            oss << abs_coef << "*";
        }
        oss << param;
        first = false;
    }
    
    // Affiche la constante
    if (std::abs(constant) > 1e-10 || first) {
        if (!first) {
            oss << (constant > 0 ? " + " : " - ");
            oss << std::abs(constant);
        } else {
            oss << constant;
        }
    }
    
    return oss.str();
}

// Conversion en format SMT-LIB2
std::string AffineTerm::toSMTLib2() const {
    if (isZero()) return "0";
    if (isConstant()) return formatNumber(constant);

    std::vector<std::string> terms;

    // Collecte tous les termes non-nuls
    for (const auto& [param, coef] : coefficients) {
        if (std::abs(coef) < 1e-10) continue;

        if (std::abs(coef - 1.0) < 1e-10) {
            // Coefficient = 1, pas besoin de multiplication
            terms.push_back(param);
        } else if (std::abs(coef + 1.0) < 1e-10) {
            // Coefficient = -1
            terms.push_back("(- " + param + ")");
        } else {
            // Coefficient général
            terms.push_back("(* " + formatNumber(coef) + " " + param + ")");
        }
    }

    // Ajoute la constante si non-nulle
    if (std::abs(constant) > 1e-10) {
        terms.push_back(formatNumber(constant));
    }
    
    // Construit l'expression finale
    if (terms.size() == 0) {
        return "0.0";
    } else if (terms.size() == 1) {
        return terms[0];
    } else {
        std::string result = "(+";
        for (const auto& term : terms) {
            result += " " + term;
        }
        result += ")";
        return result;
    }
}
