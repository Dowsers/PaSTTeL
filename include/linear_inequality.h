#ifndef LINEAR_INEQUALITY_H
#define LINEAR_INEQUALITY_H

#include <map>
#include <string>
#include "affine_term.h"

// Inégalité linéaire : Σ(coefficients[var]*var) + constant >= 0
class LinearInequality {
public:
    enum MotzkinCoefficient {
        ONE,           // λ = 1
        ZERO_AND_ONE,  // λ ∈ {0,1}
        ANYTHING       // λ ≥ 0
    };
    
    std::map<std::string, AffineTerm> coefficients;  // var -> coefficient affine
    AffineTerm constant;
    bool strict;  // true pour >, false pour >=
    MotzkinCoefficient motzkin_coef;
    
    LinearInequality();
    LinearInequality(const LinearInequality& other);
    LinearInequality& operator=(const LinearInequality&) = default;
    
    // Ajout/modification de coefficients
    void setCoefficient(const std::string& var, const AffineTerm& coef);
    AffineTerm getCoefficient(const std::string& var) const;
    
    // Opérations
    LinearInequality operator+(const LinearInequality& other) const;
    LinearInequality operator*(double scalar) const;
    void negate();
    
    // Prédicats
    bool isConstant() const;
    bool isTautology() const;

    // Construct a trivially false inequality: -1 >= 0
    static LinearInequality constructFalse();
    
    // Export
    std::string toString() const;
    std::string toSMTLib2() const;
};

#endif