#ifndef AFFINE_TERM_H
#define AFFINE_TERM_H

#include <map>
#include <string>
#include <memory>

// Terme affine: Σ(coefficients[param] * param) + constant
class AffineTerm {
public:
    std::map<std::string, double> coefficients;
    double constant;
    
    AffineTerm();                        // Constructeur par défaut
    AffineTerm(double c);       // Constructeur pour constante
    AffineTerm(const AffineTerm& other); // Constructeur de copie
    
    // Prédicats
    bool isZero() const;
    bool isConstant() const;
    
    // Opérations arithmétiques
    AffineTerm operator+(const AffineTerm& other) const;
    AffineTerm operator-(const AffineTerm& other) const;
    AffineTerm operator*(double scalar) const;
    AffineTerm& operator+=(const AffineTerm& other);
    AffineTerm& operator*=(double scalar);

    // ajouter explicitement l’opérateur d’assignation
    AffineTerm& operator=(const AffineTerm& other) {
        if (this != &other) {
            // copier les membres
            this->coefficients = other.coefficients;
            this->constant = other.constant;
            // etc.
        }
        return *this;
    }

    // Utilitaires
    void negate();
    std::string toString() const;
    std::string toSMTLib2() const;
};

#endif