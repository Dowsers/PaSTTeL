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


// ============================================================================
// CONSTRUCTEUR
// ============================================================================

std::atomic<int> MotzkinTransformation::s_instance_counter{0};

MotzkinTransformation::MotzkinTransformation()
    : m_instance_id(s_instance_counter.fetch_add(1))
{
}

// ============================================================================
// MÉTHODE PRINCIPALE : addConstraintsToSolver
// ============================================================================

void MotzkinTransformation::addConstraintsToSolver(
    const std::vector<LinearInequality>& constraints,
    const std::unordered_set<std::string>& program_vars,
    std::shared_ptr<SMTSolver> solver,
    const std::string& annotation)
{
    // ────────────────────────────────────────────────────────────────────────
    // 1. INITIALISATION
    // ────────────────────────────────────────────────────────────────────────
    m_inequalities = constraints;
    m_program_vars = program_vars;

    if (m_inequalities.empty()) {
        if (VERBOSITY == VerbosityLevel::VERBOSE)
            std::cout << "  ⚠ Motzkin: No constraints to transform" << std::endl;
        return;
    }

    // ────────────────────────────────────────────────────────────────────────
    // 2. COEFFICIENTS DE MOTZKIN
    // ────────────────────────────────────────────────────────────────────────
    //   Si coefficients non-constants (ex. SUP_INVAR) → "fixed" :
    //     énumération {0, 1} au lieu d'une variable libre → reste LRA.

    registerMotzkinCoefficients();

    // Fixed indices: coefficients assignés "1.0" par registerMotzkinCoefficients
    // mais dont motzkin_coef != ONE (ANYTHING ou ZERO_AND_ONE avec termes non-constants).
    // Ces indices seront énumérés en {0,1} pour rester en LRA.
    // !needsMotzkinCoefficient(li) && li.mMotzkinCoefficient != ONE
    std::vector<size_t> fixed_indices;
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& li = m_inequalities[i];
        bool has_free_var = (m_motzkin_coefficients[i] != "1.0");
        if (!has_free_var && li.motzkin_coef != LinearInequality::ONE) {
            fixed_indices.push_back(i);
        }
    }

    // Déclarer les variables libres et asserter λ >= 0 (hors énumération)
    for (size_t i = 0; i < m_motzkin_coefficients.size(); ++i) {
        const auto& lambda = m_motzkin_coefficients[i];
        if (lambda == "1.0") continue;
        if (!solver->variableExists(lambda)) {
            solver->declareVariable(lambda, "Real");
        }
        // λ >= 0 toujours asserté hors énumération
        solver->addAssertion("(>= " + lambda + " 0.0)");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "    " << annotation << std::endl;
        std::cout << "     Constraints: " << m_inequalities.size()
                  << ", Program vars: " << m_program_vars.size()
                  << ", Motzkin coeffs: " << m_motzkin_coefficients.size()
                  << ", Fixed (enum): " << fixed_indices.size() << std::endl;
    }

    // ────────────────────────────────────────────────────────────────────────
    // 3. CONTRAINTES HORS ÉNUMÉRATION (ZERO_AND_ONE sur variables libres)
    // ────────────────────────────────────────────────────────────────────────
    // Pour les variables libres avec ZERO_AND_ONE (termes constants) : (or (= λ 0) (= λ 1))
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& lambda = m_motzkin_coefficients[i];
        if (lambda == "1.0") continue;  // pas de variable libre → rien à faire ici
        if (m_inequalities[i].motzkin_coef == LinearInequality::ZERO_AND_ONE) {
            solver->addAssertion("(or (= " + lambda + " 0.0) (= " + lambda + " 1.0))");
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    // 4. ÉGALITÉ + CONSTANTE + STRICTNESS
    // ────────────────────────────────────────────────────────────────────────

    if (fixed_indices.empty()) {
        // Chemin simple : aucun indice à énumérer, assertions directes (LRA)
        generateEqualityConstraints(solver);
        generateConstantConstraint(solver);
        generateStrictConstraint(solver);

    } else {
        // Chemin d'énumération (LINEAR mode) :
        // Pour k indices fixed, générer 2^k branches et asserter (or branch_0 ...)
        // Chaque branche est entièrement LRA car les fixed sont 0.0 ou 1.0.
        size_t k = fixed_indices.size();
        std::vector<std::string> combo_ands;

        for (size_t combo = 0; combo < (1u << k); ++combo) {
            std::vector<std::string> coef_for = m_motzkin_coefficients;
            for (size_t bit = 0; bit < k; ++bit) {
                coef_for[fixed_indices[bit]] = ((combo >> bit) & 1) ? "1.0" : "0.0";
            }

            auto formulas = generateAllFormulas(coef_for);
            if (formulas.empty()) continue;

            if (formulas.size() == 1) {
                combo_ands.push_back(formulas[0]);
            } else {
                std::string and_str = "(and";
                for (const auto& f : formulas) and_str += " " + f;
                and_str += ")";
                combo_ands.push_back(and_str);
            }
        }

        if (!combo_ands.empty()) {
            if (combo_ands.size() == 1) {
                solver->addAssertion(combo_ands[0]);
            } else {
                std::string or_str = "(or";
                for (const auto& s : combo_ands) or_str += " " + s;
                or_str += ")";
                solver->addAssertion(or_str);
            }
        }
    }
}

// ============================================================================
// ENREGISTREMENT DES COEFFICIENTS DE MOTZKIN
// ============================================================================

void MotzkinTransformation::registerMotzkinCoefficients() {
    m_motzkin_coefficients.clear();
    for (size_t i = 0; i < m_inequalities.size(); ++i) {
        const auto& li = m_inequalities[i];
        // needsMotzkinCoefficient():
        // Une variable libre est créée seulement si :
        //   - motzkin_coef != ONE  (pas déjà fixé à 1)
        //   - ET tous les termes affines sont des constantes numériques
        //     (sinon λ×paramètre_template serait NLA → on énumérera {0,1})
        bool needs_variable = (li.motzkin_coef != LinearInequality::ONE)
                                && allAffineTermsAreConstant(li);
        if (needs_variable) {
            std::ostringstream oss;
            oss << "motzkin_" << m_instance_id << "_" << i;
            m_motzkin_coefficients.push_back(oss.str());
        } else {
            m_motzkin_coefficients.push_back("1.0");
        }
    }
}

// ============================================================================
// GÉNÉRATION DES CONTRAINTES
// ============================================================================

void MotzkinTransformation::generatePositivityConstraints(std::shared_ptr<SMTSolver> solver) {
    // Pour chaque coefficient de Motzkin: λ_i ≥ 0
    for (const auto& lambda : m_motzkin_coefficients) {
        if (lambda == "1.0") continue;  // pas de λ ≥ 0 pour les littéraux
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
    if (motzkin == "1.0") {
        return affineTermToSMT(term);  // 1.0 × term = term → LRA
    }

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

// ============================================================================
// MODE LINÉAIRE
// ============================================================================

bool MotzkinTransformation::allAffineTermsAreConstant(
    const LinearInequality& ineq) const
{
    // Un AffineTerm est "constant" si son map coefficients{} est vide
    // (aucun nom de paramètre template : SUP_INVAR_*, RANKING_C_*, ...).

    if (!ineq.constant.coefficients.empty()) return false;

    for (const auto& var : m_program_vars) {
        AffineTerm coef = ineq.getCoefficient(var);
        if (!coef.coefficients.empty()) return false;
    }
    return true;
}

std::vector<std::string> MotzkinTransformation::generateAllFormulas(
    const std::vector<std::string>& coef_for) const
{
    // Version "string-collecting" de generateEquality + generateConstant +
    // generateStrict, paramétrée par coef_for[i] (peut être "0.0", "1.0" ou
    // un nom de variable libre). Les entrées "0.0" sont ignorées.

    std::vector<std::string> formulas;

    // ── Égalité : Σ coef_for[i] * coef_i(var) = 0  (une contrainte par var)
    for (const auto& var : m_program_vars) {
        std::vector<std::string> summands;
        for (size_t i = 0; i < m_inequalities.size(); ++i) {
            if (coef_for[i] == "0.0") continue;
            AffineTerm coef = m_inequalities[i].getCoefficient(var);
            if (coef.isZero()) continue;
            summands.push_back(multiplyByMotzkin(coef, coef_for[i]));
        }
        if (summands.empty()) continue;

        std::string f = "(= ";
        if (summands.size() == 1) {
            f += summands[0];
        } else {
            f += "(+";
            for (const auto& s : summands) f += " " + s;
            f += ")";
        }
        f += " 0.0)";
        formulas.push_back(f);
    }

    // ── Constante : Σ coef_for[i] * b_i <= 0
    {
        std::vector<std::string> summands;
        for (size_t i = 0; i < m_inequalities.size(); ++i) {
            if (coef_for[i] == "0.0") continue;
            if (m_inequalities[i].constant.isZero()) continue;
            summands.push_back(
                multiplyByMotzkin(m_inequalities[i].constant, coef_for[i]));
        }
        if (!summands.empty()) {
            std::string f = "(<= ";
            if (summands.size() == 1) {
                f += summands[0];
            } else {
                f += "(+";
                for (const auto& s : summands) f += " " + s;
                f += ")";
            }
            f += " 0.0)";
            formulas.push_back(f);
        }
    }

    // ── Strictness : (Σ non-strict * b_i < 0) ∨ (Σ strict coeffs > 0)
    // Quand les deux listes sont vides → (or false false) = false : correct,
    // une branche sans inégalité active ne prouve pas l'insatisfiabilité.
    {
        std::vector<std::string> ns_summands;
        for (size_t i = 0; i < m_inequalities.size(); ++i) {
            if (coef_for[i] == "0.0" || m_inequalities[i].strict) continue;
            if (m_inequalities[i].constant.isZero()) continue;
            ns_summands.push_back(
                multiplyByMotzkin(m_inequalities[i].constant, coef_for[i]));
        }
        std::string classical;
        if (ns_summands.empty()) {
            classical = "(< 0.0 0.0)";
        } else if (ns_summands.size() == 1) {
            classical = "(< " + ns_summands[0] + " 0.0)";
        } else {
            classical = "(< (+";
            for (const auto& s : ns_summands) classical += " " + s;
            classical += ") 0.0)";
        }

        std::vector<std::string> s_coeffs;
        for (size_t i = 0; i < m_inequalities.size(); ++i) {
            if (coef_for[i] == "0.0") continue;
            if (m_inequalities[i].strict) s_coeffs.push_back(coef_for[i]);
        }
        std::string non_classical;
        if (s_coeffs.empty()) {
            non_classical = "(> 0.0 0.0)";
        } else if (s_coeffs.size() == 1) {
            non_classical = "(> " + s_coeffs[0] + " 0.0)";
        } else {
            non_classical = "(> (+";
            for (const auto& s : s_coeffs) non_classical += " " + s;
            non_classical += ") 0.0)";
        }
        formulas.push_back("(or " + classical + " " + non_classical + ")");
    }

    return formulas;
}
