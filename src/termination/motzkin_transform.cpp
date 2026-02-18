#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <iomanip>

#include "termination/motzkin_transform.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// Format a double as an SMT-LIB2 numeral (no scientific notation).
// Integer-valued doubles are output without decimal point.
static std::string formatSMTNumber(double v) {
    if (v == std::floor(v) && std::abs(v) < 1e18) {
        return std::to_string(static_cast<long long>(v));
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << v;
    return oss.str();
}

// ============================================================================
// STATIC MEMBERS
// ============================================================================

int MotzkinTransformation::s_motzkin_counter = 0;

void MotzkinTransformation::init_counter(){
    s_motzkin_counter = 0;
}

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

MotzkinTransformation::MotzkinTransformation() {
}

// ============================================================================
// MÉTHODE PRINCIPALE : addConstraintsToSolver
// ============================================================================

void MotzkinTransformation::addConstraintsToSolver(
    const std::vector<LinearInequality>& constraints,
    const std::vector<std::string>& program_vars,
    std::shared_ptr<SMTSolver> solver,
    const std::string& annotation)
{
    // ────────────────────────────────────────────────────────────────────────
    // 1. INITIALISATION
    // ────────────────────────────────────────────────────────────────────────
    m_inequalities = constraints;
    m_program_vars = std::set<std::string>(program_vars.begin(), program_vars.end());
    
    if (m_inequalities.empty()) {
        if (VERBOSITY == VerbosityLevel::VERBOSE)
            std::cout << "  ⚠ Motzkin: No constraints to transform" << std::endl;
        return;
    }
    
    // ────────────────────────────────────────────────────────────────────────
    // 2. ENREGISTREMENT DES COEFFICIENTS DE MOTZKIN
    // ────────────────────────────────────────────────────────────────────────
    
    registerMotzkinCoefficients();

    // Déclarer les coefficients de Motzkin dans le solveur
    for (const auto& lambda : m_motzkin_coefficients) {
        if (!solver->variableExists(lambda)) {
            solver->declareVariable(lambda, "Real");
        }
    }
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "    " << annotation << std::endl;
        std::cout << "     Constraints: " << m_inequalities.size()
                << ", Program vars: " << m_program_vars.size()
                << ", Motzkin coeffs: " << m_motzkin_coefficients.size() << std::endl;
    }
    // ────────────────────────────────────────────────────────────────────────
    // 3. GÉNÉRATION DES CONTRAINTES MOTZKIN
    // ────────────────────────────────────────────────────────────────────────
    
    // 3.1 Positivité: λ_i ≥ 0
    generatePositivityConstraints(solver);
    
    // 3.2 Contraintes spéciales sur certains λ_i (ONE, ZERO_AND_ONE)
    generateMotzkinCoefficientConstraints(solver);
    
    // 3.3 Égalité: Σ_i λ_i * coef_i(var) = 0 pour chaque variable de programme
    generateEqualityConstraints(solver);
    
    // 3.4 Constante: Σ_i λ_i * b_i ≤ 0
    generateConstantConstraint(solver);
    
    // 3.5 Strictness: (Σ_i λ_i * b_i < 0) ∨ (Σ_j μ_j > 0)
    generateStrictConstraint(solver);
}

// ============================================================================
// ENREGISTREMENT DES COEFFICIENTS DE MOTZKIN
// ============================================================================

void MotzkinTransformation::registerMotzkinCoefficients() {
    m_motzkin_coefficients.clear();
    
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        std::ostringstream oss;
        oss << "motzkin_" << s_motzkin_counter << "_" << i;
        m_motzkin_coefficients.push_back(oss.str());
    }
    
    s_motzkin_counter++;
}

// ============================================================================
// GÉNÉRATION DES CONTRAINTES
// ============================================================================

void MotzkinTransformation::generatePositivityConstraints(std::shared_ptr<SMTSolver> solver) {
    // Pour chaque coefficient de Motzkin: λ_i ≥ 0
    for (const auto& lambda : m_motzkin_coefficients) {
        std::ostringstream constraint;
        constraint << "(>= " << lambda << " 0.0)";
        solver->addAssertion(constraint.str());
    }
}

void MotzkinTransformation::generateMotzkinCoefficientConstraints(std::shared_ptr<SMTSolver> solver) {
    // Contraintes spéciales selon le type de coefficient
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& ineq = m_inequalities[i];
        const auto& lambda = m_motzkin_coefficients[i];
        
        switch (ineq.motzkin_coef) {
            case LinearInequality::ONE:
                // λ_i = 1 (coefficient fixe)
                {
                    std::ostringstream constraint;
                    constraint << "(= " << lambda << " 1.0)";
                    solver->addAssertion(constraint.str());
                }
                break;
                
            case LinearInequality::ZERO_AND_ONE:
                // λ_i ∈ {0, 1} (coefficient booléen)
                {
                    std::ostringstream constraint;
                    constraint << "(or (= " << lambda << " 0.0) (= " << lambda << " 1.0))";
                    solver->addAssertion(constraint.str());
                }
                break;
                
            case LinearInequality::ANYTHING:
                // Pas de contrainte supplémentaire (juste λ_i ≥ 0)
                break;
        }
    }
}

void MotzkinTransformation::generateEqualityConstraints(std::shared_ptr<SMTSolver> solver) {
    // Pour chaque variable de programme: Σ_i λ_i * coef_i(var) = 0
    // C'est la contrainte clé qui ÉLIMINE les variables de programme
    
    for (const auto& var : m_program_vars) {
        std::vector<std::string> summands;
        
        for (size_t i = 0; i < m_inequalities.size(); ++i) {
            const auto& ineq = m_inequalities[i];
            const auto& lambda = m_motzkin_coefficients[i];
            
            // Récupérer le coefficient de cette variable dans cette inégalité
            AffineTerm coef = ineq.getCoefficient(var);
            
            if (coef.isZero()) {
                continue;  // Pas de contribution
            }
            
            // Créer le terme: λ_i * coef_i(var)
            std::string term = multiplyByMotzkin(coef, lambda);
            summands.push_back(term);
        }
        
        if (summands.empty()) {
            // Tous les coefficients sont nuls pour cette variable
            continue;
        }
        
        // Construire: (Σ_i λ_i * coef_i(var)) = 0
        std::ostringstream constraint;
        constraint << "(= ";
        
        if (summands.size() == 1) {
            constraint << summands[0];
        } else {
            constraint << "(+";
            for (const auto& s : summands) {
                constraint << " " << s;
            }
            constraint << ")";
        }
        
        constraint << " 0.0)";
        solver->addAssertion(constraint.str());
    }
}

void MotzkinTransformation::generateConstantConstraint(std::shared_ptr<SMTSolver> solver) {
    // Σ_i λ_i * b_i ≤ 0
    
    std::vector<std::string> summands;
    
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& ineq = m_inequalities[i];
        const auto& lambda = m_motzkin_coefficients[i];
        
        if (ineq.constant.isZero()) {
            continue;
        }
        
        // Créer le terme: λ_i * b_i
        std::string term = multiplyByMotzkin(ineq.constant, lambda);
        summands.push_back(term);
    }
    
    if (summands.empty()) {
        // Pas de constantes, donc 0 ≤ 0 est toujours vrai
        return;
    }
    
    // Construire: (Σ_i λ_i * b_i) ≤ 0
    std::ostringstream constraint;
    constraint << "(<= ";
    
    if (summands.size() == 1) {
        constraint << summands[0];
    } else {
        constraint << "(+";
        for (const auto& s : summands) {
            constraint << " " << s;
        }
        constraint << ")";
    }
    
    constraint << " 0.0)";
    solver->addAssertion(constraint.str());
}

void MotzkinTransformation::generateStrictConstraint(std::shared_ptr<SMTSolver> solver) {
    // (Σ_i λ_i * b_i < 0) ∨ (Σ_j μ_j > 0)
    // où i parcourt les inégalités NON-STRICTES et j les STRICTES
    
    // ────────────────────────────────────────────────────────────────────────
    // PARTIE 1: Σ_i λ_i * b_i < 0 (inégalités non-strictes uniquement)
    // ────────────────────────────────────────────────────────────────────────
    
    std::vector<std::string> non_strict_summands;
    
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& ineq = m_inequalities[i];
        const auto& lambda = m_motzkin_coefficients[i];
        
        if (ineq.strict) {
            continue;  // Ignorer les strictes pour cette partie
        }
        
        if (ineq.constant.isZero()) {
            continue;
        }
        
        std::string term = multiplyByMotzkin(ineq.constant, lambda);
        non_strict_summands.push_back(term);
    }
    
    std::string classical_part;
    if (non_strict_summands.empty()) {
        classical_part = "(< 0.0 0.0)";  // Toujours faux
    } else if (non_strict_summands.size() == 1) {
        classical_part = "(< " + non_strict_summands[0] + " 0.0)";
    } else {
        classical_part = "(< (+";
        for (const auto& s : non_strict_summands) {
            classical_part += " " + s;
        }
        classical_part += ") 0.0)";
    }
    
    // ────────────────────────────────────────────────────────────────────────
    // PARTIE 2: Σ_j μ_j > 0 (inégalités strictes uniquement)
    // ────────────────────────────────────────────────────────────────────────
    
    std::vector<std::string> strict_coeffs;
    
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& ineq = m_inequalities[i];
        const auto& lambda = m_motzkin_coefficients[i];
        
        if (ineq.strict) {
            strict_coeffs.push_back(lambda);
        }
    }
    
    std::string non_classical_part;
    if (strict_coeffs.empty()) {
        non_classical_part = "(> 0.0 0.0)";  // Toujours faux
    } else if (strict_coeffs.size() == 1) {
        non_classical_part = "(> " + strict_coeffs[0] + " 0.0)";
    } else {
        non_classical_part = "(> (+";
        for (const auto& s : strict_coeffs) {
            non_classical_part += " " + s;
        }
        non_classical_part += ") 0.0)";
    }
    
    // ────────────────────────────────────────────────────────────────────────
    // DISJONCTION FINALE
    // ────────────────────────────────────────────────────────────────────────
    
    std::ostringstream constraint;
    constraint << "(or " << classical_part << " " << non_classical_part << ")";
    
    solver->addAssertion(constraint.str());
}

// ============================================================================
// UTILITAIRES SMT
// ============================================================================

std::string MotzkinTransformation::affineTermToSMT(const AffineTerm& term) const {
    if (term.isConstant()) {
        // Juste une constante
        return formatSMTNumber(term.constant);
    }
    
    std::vector<std::string> summands;
    
    // Ajouter les termes paramétriques
    for (const auto& [param, coef] : term.coefficients) {
        if (std::abs(coef) < 1e-10) {
            continue;  // Coefficient nul
        }
        
        if (std::abs(coef - 1.0) < 1e-10) {
            // Coefficient = 1
            summands.push_back(param);
        } else if (std::abs(coef + 1.0) < 1e-10) {
            // Coefficient = -1
            summands.push_back("(- " + param + ")");
        } else {
            // Coefficient général
            summands.push_back("(* " + formatSMTNumber(coef) + " " + param + ")");
        }
    }
    
    // Ajouter la constante
    if (std::abs(term.constant) > 1e-10) {
        summands.push_back(formatSMTNumber(term.constant));
    }
    
    if (summands.empty()) {
        return "0";
    } else if (summands.size() == 1) {
        return summands[0];
    } else {
        std::string result = "(+";
        for (const auto& s : summands) {
            result += " " + s;
        }
        result += ")";
        return result;
    }
}

std::string MotzkinTransformation::multiplyByMotzkin(
    const AffineTerm& term,
    const std::string& motzkin) const
{
    if (term.isZero()) {
        return "0";
    }
    
    if (term.isConstant()) {
        // term est juste une constante c
        if (std::abs(term.constant - 1.0) < 1e-10) {
            return motzkin;  // 1 * λ = λ
        } else if (std::abs(term.constant + 1.0) < 1e-10) {
            return "(- " + motzkin + ")";  // -1 * λ = -λ
        } else {
            return "(* " + formatSMTNumber(term.constant) + " " + motzkin + ")";
        }
    }

    // Cas général: term est une expression affine avec paramètres
    // On veut: λ * (a*PARAM_1 + b*PARAM_2 + c)
    // = (a * λ * PARAM_1) + (b * λ * PARAM_2) + (c * λ)

    std::vector<std::string> summands;

    // Termes paramétriques
    for (const auto& [param, coef] : term.coefficients) {
        if (std::abs(coef) < 1e-10) {
            continue;
        }

        if (std::abs(coef - 1.0) < 1e-10) {
            // coef = 1: λ * param
            summands.push_back("(* " + motzkin + " " + param + ")");
        } else {
            // coef général: coef * λ * param
            summands.push_back("(* " + formatSMTNumber(coef) + " (* " + motzkin + " " + param + "))");
        }
    }

    // Constante
    if (std::abs(term.constant) > 1e-10) {
        summands.push_back("(* " + formatSMTNumber(term.constant) + " " + motzkin + ")");
    }
    
    if (summands.empty()) {
        return "0";
    } else if (summands.size() == 1) {
        return summands[0];
    } else {
        std::string result = "(+";
        for (const auto& s : summands) {
            result += " " + s;
        }
        result += ")";
        return result;
    }
}
