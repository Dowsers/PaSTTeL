#include <sstream>
#include <vector>

#include "linear_inequality.h"
#include "utiles.h"


// Constructeur par défaut - crée 0 >= 0
LinearInequality::LinearInequality()
    : constant(AffineTerm(0.0)), strict(false), motzkin_coef(ANYTHING) {}

// Construit une inégalité trivialement fausse : -1 >= 0
LinearInequality LinearInequality::constructFalse() {
    LinearInequality li;
    li.constant = AffineTerm(-1.0);
    li.strict = false;
    return li;
}

// Constructeur de copie
LinearInequality::LinearInequality(const LinearInequality& other)
    : coefficients(other.coefficients),
    constant(other.constant),
    strict(other.strict),
    motzkin_coef(other.motzkin_coef) {}

// Définit le coefficient affine pour une variable
void LinearInequality::setCoefficient(const std::string& var, const AffineTerm& coef) {
    if (coef.isZero()) {
        // Si coefficient nul, on supprime la variable
        coefficients.erase(var);
    } else {
        coefficients[var] = coef;
    }
}

// Récupère le coefficient d'une variable (0 si absent)
AffineTerm LinearInequality::getCoefficient(const std::string& var) const {
    auto it = coefficients.find(var);
    if (it != coefficients.end()) {
        return it->second;
    }
    return AffineTerm(0.0);  // Retourne 0 si variable absente
}

// Addition de deux inégalités
LinearInequality LinearInequality::operator+(const LinearInequality& other) const {
    LinearInequality result(*this);
    
    // Additionne les coefficients de chaque variable
    for (const auto& [var, coef] : other.coefficients) {
        if (result.coefficients.find(var) != result.coefficients.end()) {
            result.coefficients[var] = result.coefficients[var] + coef;
        } else {
            result.coefficients[var] = coef;
        }
    }
    
    // Additionne les constantes
    result.constant = result.constant + other.constant;
    
    // Nettoie les coefficients nuls
    for (auto it = result.coefficients.begin(); it != result.coefficients.end();) {
        if (it->second.isZero()) {
            it = result.coefficients.erase(it);
        } else {
            ++it;
        }
    }
    
    // L'addition de deux inégalités non-strictes reste non-stricte
    // L'addition avec au moins une stricte devient stricte
    result.strict = this->strict || other.strict;
    
    // Le coefficient de Motzkin devient le plus restrictif
    if (this->motzkin_coef == ONE || other.motzkin_coef == ONE) {
        result.motzkin_coef = ONE;
    } else if (this->motzkin_coef == ZERO_AND_ONE || other.motzkin_coef == ZERO_AND_ONE) {
        result.motzkin_coef = ZERO_AND_ONE;
    } else {
        result.motzkin_coef = ANYTHING;
    }
    
    return result;
}

// Multiplication par un scalaire
LinearInequality LinearInequality::operator*(double scalar) const {
    LinearInequality result(*this);
    
    // Multiplie tous les coefficients
    for (auto& [var, coef] : result.coefficients) {
        coef = coef * scalar;
    }
    
    // Multiplie la constante
    result.constant = result.constant * scalar;
    
    // Si multiplication par négatif, inverse le sens de l'inégalité
    if (scalar < 0) {
        result.negate();
        // Pour une inégalité stricte : a > 0 devient -a < 0 soit -a > 0 après normalisation
        // Donc on garde strict = true
    }
    
    return result;
}

// Négation (inverse le signe de tous les termes)
void LinearInequality::negate() {
    // Négation des coefficients
    for (auto& [var, coef] : coefficients) {
        coef.negate();
    }
    
    // Négation de la constante
    constant.negate();
    
    // Note: le sens de l'inégalité (strict) n'est PAS changé ici
    // car on représente toujours sous forme ... >= 0 ou ... > 0
}

// Vérifie si l'inégalité est purement constante
bool LinearInequality::isConstant() const {
    return coefficients.empty();
}

// Vérifie si c'est une tautologie (toujours vraie)
bool LinearInequality::isTautology() const {
    if (!isConstant()) return false;
    
    // L'inégalité est : constant >= 0 (ou > 0 si strict)
    if (!constant.isConstant()) return false;
    
    if (strict) {
        // Pour strict : constant > 0 est tautologie si constant > 0
        return constant.constant > 1e-10;
    } else {
        // Pour non-strict : constant >= 0 est tautologie si constant >= 0
        return constant.constant >= -1e-10;
    }
}

// Conversion en string lisible
std::string LinearInequality::toString() const {
    std::ostringstream oss;
    bool first = true;
    
    // Affiche les termes avec variables
    for (const auto& [var, coef] : coefficients) {
        if (coef.isZero()) continue;
        
        if (!first) oss << " + ";
        
        // Si le coefficient est simple (juste une constante)
        if (coef.isConstant()) {
            if (std::abs(coef.constant - 1.0) < 1e-10) {
                oss << var;
            } else if (std::abs(coef.constant + 1.0) < 1e-10) {
                oss << "-" << var;
            } else {
                oss << coef.constant << "*" << var;
            }
        } else {
            // Coefficient paramétrique
            oss << "(" << coef.toString() << ")*" << var;
        }
        first = false;
    }
    
    // Affiche la constante
    if (!constant.isZero()) {
        if (!first) oss << " + ";
        if (constant.isConstant()) {
            oss << constant.constant;
        } else {
            oss << "(" << constant.toString() << ")";
        }
        first = false;
    }
    
    // Si vide, affiche 0
    if (first) {
        oss << "0";
    }
    
    // Affiche le comparateur
    oss << (strict ? " > 0" : " >= 0");
    
    return oss.str();
}

// Conversion en format SMT-LIB2
std::string LinearInequality::toSMTLib2() const {
    // Helper: convertir v_arr[v_idx] → (select v_arr v_idx)
    // Les identifiants SMT-LIB2 quotés |...| sont des scalaires même s'ils contiennent '[' :
    auto convertArrayNotation = [](const std::string& var) -> std::string {
        if (var.size() >= 2 && var.front() == '|' && var.back() == '|')
            return var;
        size_t bracket_pos = var.find('[');
        if (bracket_pos != std::string::npos) {
            size_t close_bracket = var.find(']', bracket_pos);
            std::string array_name = var.substr(0, bracket_pos);
            std::string index = var.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
            return "(select " + array_name + " " + index + ")";
        }
        return var;
    };
    
    std::vector<std::string> positive_terms;  // Termes positifs
    std::vector<std::string> negative_terms;  // Termes négatifs
    
    // Collecte les termes des coefficients principaux
    for (const auto& [var, coef] : coefficients) {
        if (coef.isZero()) continue;
        
        std::string var_smt = convertArrayNotation(var);
        
        // Si coefficient constant
        if (coef.isConstant()) {
            double val = coef.constant;
            if (std::abs(val - 1.0) < 1e-10) {
                positive_terms.push_back(var_smt);
            } else if (std::abs(val + 1.0) < 1e-10) {
                negative_terms.push_back(var_smt);
            } else if (val > 0) {
                positive_terms.push_back("(* " + formatNumber(val) + " " + var_smt + ")");
            } else {
                negative_terms.push_back("(* " + formatNumber(-val) + " " + var_smt + ")");
            }
        }
        // Coefficient paramétrique
        else {
            std::string coef_str = coef.toSMTLib2();
            positive_terms.push_back("(* " + coef_str + " " + var_smt + ")");
        }
    }
    
    // Traiter constant.coefficients (variables dans la partie constante)
    for (const auto& [var, coef_val] : constant.coefficients) {
        std::string var_smt = convertArrayNotation(var);
        
        if (std::abs(coef_val - 1.0) < 1e-10) {
            positive_terms.push_back(var_smt);
        } else if (std::abs(coef_val + 1.0) < 1e-10) {
            negative_terms.push_back(var_smt);
        } else if (coef_val > 0) {
            positive_terms.push_back("(* " + formatNumber(coef_val) + " " + var_smt + ")");
        } else {
            negative_terms.push_back("(* " + formatNumber(-coef_val) + " " + var_smt + ")");
        }
    }
    
    // Ajouter la partie constante numérique
    if (std::abs(constant.constant) > 1e-10) {
        if (constant.constant > 0) {
            positive_terms.push_back(formatNumber(constant.constant));
        } else {
            negative_terms.push_back(formatNumber(-constant.constant));
        }
    }
    
    // Construction de l'expression
    std::string expr;
    
    if (positive_terms.empty() && negative_terms.empty()) {
        expr = "0";
    } 
    else if (negative_terms.empty()) {
        // Que des termes positifs
        if (positive_terms.size() == 1) {
            expr = positive_terms[0];
        } else {
            expr = "(+";
            for (const auto& t : positive_terms) expr += " " + t;
            expr += ")";
        }
    }
    else if (positive_terms.empty()) {
        // Que des termes négatifs → expression négative
        if (negative_terms.size() == 1) {
            expr = "(- " + negative_terms[0] + ")";
        } else {
            expr = "(- (+";
            for (const auto& t : negative_terms) expr += " " + t;
            expr += "))";
        }
    }
    else {
        // Mélange de positifs et négatifs → soustraction
        std::string pos_expr;
        if (positive_terms.size() == 1) {
            pos_expr = positive_terms[0];
        } else {
            pos_expr = "(+";
            for (const auto& t : positive_terms) pos_expr += " " + t;
            pos_expr += ")";
        }
        
        std::string neg_expr;
        if (negative_terms.size() == 1) {
            neg_expr = negative_terms[0];
        } else {
            neg_expr = "(+";
            for (const auto& t : negative_terms) neg_expr += " " + t;
            neg_expr += ")";
        }
        
        expr = "(- " + pos_expr + " " + neg_expr + ")";
    }
    
    // Construire l'inégalité finale
    std::string op = strict ? ">" : ">=";
    return "(" + op + " " + expr + " 0)";
}
