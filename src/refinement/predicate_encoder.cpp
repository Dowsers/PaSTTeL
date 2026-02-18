#include <sstream>
#include <iostream>
#include <cmath>
#include <stdexcept>

#include "refinement/predicate_encoder.h"

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

PredicateEncoder::PredicateEncoder() {
}

// ============================================================================
// ENCODAGE PRINCIPAL
// ============================================================================

PredicateEncoder::Predicates PredicateEncoder::encode(
    const GenericTerminationSynthesizer::RankingFunction& rf,
    const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
    const LassoProgram& lasso)
{
    std::cout << "\n=== PredicateEncoder::encode ===" << std::endl;
    std::cout << "Program variables: ";
    for (const auto& v : lasso.program_vars) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
    std::cout << "Num SI: " << sis.size() << std::endl;

    Predicates preds;

    // 1. Stem precondition (encodage depuis les contraintes du stem)
    preds.stem_precondition = encodeStemPrecondition(lasso);
    std::cout << "✓ stem_precondition encoded" << std::endl;

    // 2. Stem postcondition (SI(x) après le stem)
    preds.stem_postcondition = encodeStemPostcondition(sis, lasso);
    std::cout << "✓ stem_postcondition encoded" << std::endl;

    // 3. SI conjunction
    preds.si_conjunction = encodeSIConjunction(sis, lasso, false);
    std::cout << "✓ si_conjunction encoded" << std::endl;

    // 4. Rank decrease and bound : f(x') < f(x) ∧ f(x) ≥ 0
    preds.rank_decrease_and_bound = encodeRankDecreaseAndBound(rf, lasso);
    std::cout << "✓ rank_decrease_and_bound encoded" << std::endl;

    // 5. Rank equality : f(x') = f(x)
    preds.rank_equality = encodeRankEquality(rf, lasso);
    std::cout << "✓ rank_equality encoded" << std::endl;

    // 6. Rank eq and SI : f(x') = f(x) ∧ SI(x')
    preds.rank_eq_and_si = encodeRankEqAndSI(rf, sis, lasso);
    std::cout << "✓ rank_eq_and_si encoded" << std::endl;

    // 7. Honda predicate
    preds.honda_predicate = encodeHondaPredicate(rf, sis, lasso);
    std::cout << "✓ honda_predicate encoded" << std::endl;

    std::cout << "✓ All predicates encoded" << std::endl;

    return preds;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void PredicateEncoder::printPredicates(const Predicates& preds) const {
    std::cout << "\n╔═══════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║         ENCODED PREDICATES (SMT-LIB2)             ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════╝" << std::endl;
    
    std::cout << "\n[1] stem_precondition:" << std::endl;
    std::cout << "    " << preds.stem_precondition << std::endl;
    
    std::cout << "\n[2] stem_postcondition:" << std::endl;
    std::cout << "    " << preds.stem_postcondition << std::endl;
    
    std::cout << "\n[3] si_conjunction:" << std::endl;
    std::cout << "    " << preds.si_conjunction << std::endl;
    
    std::cout << "\n[4] rank_decrease_and_bound:" << std::endl;
    std::cout << "    " << preds.rank_decrease_and_bound << std::endl;
    
    std::cout << "\n[5] rank_equality:" << std::endl;
    std::cout << "    " << preds.rank_equality << std::endl;
    
    std::cout << "\n[6] rank_eq_and_si:" << std::endl;
    std::cout << "    " << preds.rank_eq_and_si << std::endl;
    
    std::cout << "\n[7] honda_predicate:" << std::endl;
    std::cout << "    " << preds.honda_predicate << std::endl;
    
    std::cout << "\n═══════════════════════════════════════════════════" << std::endl;
}

// ============================================================================
// CONSTRUCTION f(x) / f(x')
// ============================================================================

std::string PredicateEncoder::buildRankingFunction(
    const GenericTerminationSynthesizer::RankingFunction& rf,
    const std::vector<std::string>& vars,
    bool primed,
    const LassoProgram& lasso) const
{
    std::ostringstream oss;
    oss << "(+";
    
    // Constante
    oss << " " << rf.constant;
    
    // Termes linéaires avec variables appropriées
    for (size_t i = 0; i < vars.size(); ++i) {
        const auto& var = vars[i];
        auto it = rf.coefficients.find(var);
        if (it != rf.coefficients.end() && !isZero(it->second)) {
            std::string var_name = primed ? lasso.loop.getSSAVar(var, primed) : var;
            oss << " (* " << it->second << " " << var_name << ")";
        }
    }
    
    oss << ")";
    return oss.str();
}

// ============================================================================
// CONSTRUCTION SI(x) / SI(x')
// ============================================================================

std::string PredicateEncoder::buildSupportingInvariant(
    const GenericTerminationSynthesizer::SupportingInvariant& si,
    const std::vector<std::string>& vars,
    bool primed,
    const LassoProgram& lasso) const
{
    std::ostringstream expr;
    expr << "(+";
    
    // Constante
    expr << " " << si.constant;
    
    // Termes linéaires
    for (size_t i = 0; i < vars.size(); ++i) {
        const auto& var = vars[i];
        auto it = si.coefficients.find(var);
        if (it != si.coefficients.end() && !isZero(it->second)) {
            std::string var_name = primed ? lasso.loop.getSSAVar(var, primed) : var;
            expr << " (* " << it->second << " " << var_name << ")";
        }
    }
    
    expr << ")";
    
    // Inégalité (strict ou non-strict)
    std::string op = si.is_strict ? ">" : ">=";
    return "(" + op + " " + expr.str() + " 0)";
}

// ============================================================================
// ENCODAGE DES PRÉDICATS PRINCIPAUX
// ============================================================================

std::string PredicateEncoder::encodeStemPrecondition(const LassoProgram& lasso) const {
    if (lasso.stem.polyhedra.empty()) {
        return "true";
    }
    return encodeTransition(lasso.stem);
}

std::string PredicateEncoder::encodeStemPostcondition(
    const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
    const LassoProgram& lasso) const
{
    return encodeSIConjunction(sis, lasso, false);
}

std::string PredicateEncoder::encodeHondaPredicate(
    const GenericTerminationSynthesizer::RankingFunction& rf,
    const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
    const LassoProgram& lasso) const
{
    std::string si_conj = encodeSIConjunction(sis, lasso, false);
    std::string rank_dec = encodeRankDecreaseAndBound(rf, lasso);
    
    if (si_conj == "true") {
        return rank_dec;
    }
    
    return "(and " + si_conj + " " + rank_dec + ")";
}

std::string PredicateEncoder::encodeRankDecreaseAndBound(
    const GenericTerminationSynthesizer::RankingFunction& rf,
    const LassoProgram& lasso) const
{
    std::string f_x = buildRankingFunction(rf, lasso.program_vars, false, lasso);
    std::string f_x_prime = buildRankingFunction(rf, lasso.program_vars, true, lasso);
    
    return "(and (< " + f_x_prime + " " + f_x + ") (>= " + f_x + " 0))";
}

std::string PredicateEncoder::encodeRankEquality(
    const GenericTerminationSynthesizer::RankingFunction& rf,
    const LassoProgram& lasso) const
{
    std::string f_x = buildRankingFunction(rf, lasso.program_vars, false, lasso);
    std::string f_x_prime = buildRankingFunction(rf, lasso.program_vars, true, lasso);
    
    return "(= " + f_x_prime + " " + f_x + ")";
}

std::string PredicateEncoder::encodeSIConjunction(
    const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
    const LassoProgram& lasso,
    bool primed) const
{
    if (sis.empty()) {
        return "true";
    }
    
    std::vector<std::string> si_formulas;
    for (const auto& si : sis) {
        si_formulas.push_back(buildSupportingInvariant(si, lasso.program_vars, primed, lasso));
    }
    
    if (si_formulas.size() == 1) {
        return si_formulas[0];
    }
    
    return "(and " + join(si_formulas, " ") + ")";
}

std::string PredicateEncoder::encodeRankEqAndSI(
    const GenericTerminationSynthesizer::RankingFunction& rf,
    const std::vector<GenericTerminationSynthesizer::SupportingInvariant>& sis,
    const LassoProgram& lasso) const
{
    std::string rank_eq = encodeRankEquality(rf, lasso);
    std::string si_primed = encodeSIConjunction(sis, lasso, true);
    
    if (si_primed == "true") {
        return rank_eq;
    }
    
    return "(and " + rank_eq + " " + si_primed + ")";
}

// ============================================================================
// UTILITAIRES POUR L'ENCODAGE DES CONTRAINTES DU STEM
// ============================================================================

std::string PredicateEncoder::encodeInequality(const LinearInequality& ineq) const {
    std::ostringstream expr;
    expr << "(+";
    
    bool has_terms = false;
    
    // Coefficients des variables
    for (const auto& [var, coef] : ineq.coefficients) {
        if (coef.coefficients.empty()) {
            // Coefficient concret (pas paramétrique)
            double val = coef.constant;
            if (!isZero(val)) {
                expr << " (* " << val << " " << var << ")";
                has_terms = true;
            }
        }
    }
    
    // Constante
    if (ineq.constant.coefficients.empty()) {
        double const_val = ineq.constant.constant;
        if (!isZero(const_val) || !has_terms) {
            expr << " " << const_val;
            has_terms = true;
        }
    }
    
    if (!has_terms) {
        expr << " 0";
    }
    
    expr << ")";
    
    // Inégalité
    if (ineq.strict) {
        return "(> " + expr.str() + " 0)";
    } else {
        return "(>= " + expr.str() + " 0)";
    }
}

std::string PredicateEncoder::encodePolyhedron(const std::vector<LinearInequality>& polyhedron) const {
    if (polyhedron.empty()) {
        return "true";
    }
    
    if (polyhedron.size() == 1) {
        return encodeInequality(polyhedron[0]);
    }
    
    std::vector<std::string> constraints;
    for (const auto& ineq : polyhedron) {
        constraints.push_back(encodeInequality(ineq));
    }
    
    return "(and " + join(constraints, " ") + ")";
}

std::string PredicateEncoder::encodeTransition(const LinearTransition& transition) const {
    if (transition.polyhedra.empty()) {
        return "true";
    }
    
    if (transition.polyhedra.size() == 1) {
        return encodePolyhedron(transition.polyhedra[0]);
    }
    
    std::vector<std::string> disjuncts;
    for (const auto& poly : transition.polyhedra) {
        disjuncts.push_back(encodePolyhedron(poly));
    }
    
    return "(or " + join(disjuncts, " ") + ")";
}

// ============================================================================
// UTILITAIRES GÉNÉRAUX
// ============================================================================


bool PredicateEncoder::isZero(double value) const {
    return std::abs(value) < 1e-9;
}

std::string PredicateEncoder::join(const std::vector<std::string>& strings, const std::string& separator) const {
    if (strings.empty()) {
        return "";
    }
    
    std::ostringstream oss;
    oss << strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        oss << separator << strings[i];
    }
    return oss.str();
}