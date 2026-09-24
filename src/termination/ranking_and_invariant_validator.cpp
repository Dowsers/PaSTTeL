#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>

#include "termination/ranking_and_invariant_validator.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// PRIMITIVES
//
// An obligation holds on the loop iff (loop ∧ valid SIs(x) ∧ ¬obligation) is
// proven UNSAT, where
//   - loop is the disjunction of the polyhedra of the loop transition;
//   - SAT and inconclusive answers (unknown, timeout, interruption) fail;
//   - a non-zero coefficient on a name that is not a program variable, or that
//     has no SSA name on the evaluated side, rejects the certificate;
//   - terms are linear expressions with exact rational coefficients; an atom
//     (op e 0) is emitted with e scaled by the lcm of its denominators.
// ============================================================================

namespace {

std::string smtAnd(const std::vector<std::string>& atoms) {
    if (atoms.empty()) return "true";
    if (atoms.size() == 1) return atoms[0];
    std::ostringstream o;
    o << "(and";
    for (const auto& a : atoms) o << " " << a;
    o << ")";
    return o.str();
}

std::string smtOr(const std::vector<std::string>& atoms) {
    if (atoms.empty()) return "false";
    if (atoms.size() == 1) return atoms[0];
    std::ostringstream o;
    o << "(or";
    for (const auto& a : atoms) o << " " << a;
    o << ")";
    return o.str();
}

std::string transitionFormula(const LinearTransition& t) {
    std::vector<std::string> disjuncts;
    for (const auto& poly : t.polyhedra) {
        std::vector<std::string> atoms;
        for (const auto& ineq : poly) atoms.push_back(ineq.toSMTLib2());
        disjuncts.push_back(smtAnd(atoms));
    }
    return smtOr(disjuncts);
}

// sum_s coeffs[s] * s + constant, s ranging over SSA names.
struct LinExpr {
    std::map<std::string, Rational> coeffs;
    Rational constant;
};

LinExpr operator+(LinExpr a, const LinExpr& b) {
    for (const auto& [s, c] : b.coeffs) a.coeffs[s] = a.coeffs[s].add(c);
    a.constant = a.constant.add(b.constant);
    return a;
}

LinExpr operator-(LinExpr a, const LinExpr& b) {
    for (const auto& [s, c] : b.coeffs) a.coeffs[s] = a.coeffs[s].sub(c);
    a.constant = a.constant.sub(b.constant);
    return a;
}

LinExpr operator-(LinExpr a, const Rational& c) {
    a.constant = a.constant.sub(c);
    return a;
}

// (op e 0), e multiplied by the lcm of its denominators (> 0, so the relation
// is unchanged).
std::string atom(const char* op, const LinExpr& e) {
    BigInt l = e.constant.den;
    for (const auto& [s, c] : e.coeffs) l = l / Rational::gcd_ll(l, c.den) * c.den;
    auto numeral = [&l](const Rational& r) {
        const BigInt n = r.num * (l / r.den);
        return n < 0 ? "(- " + toStringBigInt(-n) + ")" : toStringBigInt(n);
    };
    std::vector<std::string> parts;
    for (const auto& [s, c] : e.coeffs)
        if (!c.isZero()) parts.push_back("(* " + numeral(c) + " " + s + ")");
    if (!e.constant.isZero() || parts.empty()) parts.push_back(numeral(e.constant));
    std::ostringstream o;
    o << "(" << op << " ";
    if (parts.size() == 1) {
        o << parts[0];
    } else {
        o << "(+";
        for (const auto& t : parts) o << " " << t;
        o << ")";
    }
    o << " 0)";
    return o.str();
}

// sum_v c_v * ssa(v) + constant, over the lasso's program variables.
bool linearTerm(const LassoProgram& lasso,
                const std::map<std::string, Rational>& coefficients,
                const Rational& constant,
                const std::map<std::string, std::string>& ssa,
                LinExpr& out, std::string& why) {
    const std::set<std::string> program_vars(lasso.program_vars.begin(), lasso.program_vars.end());
    out = LinExpr{{}, constant};
    for (const auto& [var, coef] : coefficients) {
        if (coef.isZero()) continue;
        if (!program_vars.count(var)) {
            why = "coefficient on '" + var + "', which is not a program variable of the lasso";
            return false;
        }
        auto it = ssa.find(var);
        if (it == ssa.end()) {
            why = "no SSA name for '" + var + "' on the side of the transition it is evaluated on";
            return false;
        }
        out.coeffs[it->second] = out.coeffs[it->second].add(coef);
    }
    return true;
}

// true iff the conjunction of `assertions` is proven UNSAT.
bool provenUnsat(SMTSolverInterface* solver, const std::vector<std::string>& assertions) {
    solver->push();
    for (const auto& a : assertions) solver->addAssertion(a);
    const SatResult r = solver->checkSatResult();
    solver->pop();
    return r == SatResult::UNSAT;
}

bool allDeltasPositive(const std::vector<RankingFunction>& fs) {
    for (const auto& f : fs)
        if (!(f.delta > 0)) return false;
    return true;
}

}  // namespace

struct RankingAndInvariantValidator::LoopTerms {
    std::vector<LinExpr> in, out;
};

bool RankingAndInvariantValidator::buildLoopTerms(
    const LassoProgram& lasso, const std::vector<RankingFunction>& fs,
    LoopTerms& terms, std::string& why) {
    terms.in.clear();
    terms.out.clear();
    for (const auto& f : fs) {
        LinExpr tin, tout;
        if (!linearTerm(lasso, f.coefficients, f.constant, lasso.loop.var_to_ssa_in, tin, why) ||
            !linearTerm(lasso, f.coefficients, f.constant, lasso.loop.var_to_ssa_out, tout, why))
            return false;
        terms.in.push_back(tin);
        terms.out.push_back(tout);
    }
    return true;
}

// ============================================================================
// CONSTRUCTEUR / CONTEXTE
// ============================================================================

RankingAndInvariantValidator::RankingAndInvariantValidator() {
}

void RankingAndInvariantValidator::registerProgramVariablesToSolver(
    SMTSolverInterface* solver,
    const LassoProgram& lasso) {
    lasso.declareSolverContext(solver, true);
}

std::vector<std::string> RankingAndInvariantValidator::loopContext(const LassoProgram& lasso) const {
    std::vector<std::string> ctx{transitionFormula(lasso.loop)};
    for (const auto& si : valid_sis) {
        LinExpr t;
        std::string why;
        // valid_sis only holds invariants whose terms were already built
        // successfully by validateSingleSI.
        linearTerm(lasso, si.coefficients, si.constant, lasso.loop.var_to_ssa_in, t, why);
        ctx.push_back(atom(si.is_strict ? ">" : ">=", t));
    }
    return ctx;
}

bool RankingAndInvariantValidator::holdsOnLoop(const LassoProgram& lasso,
                                               const std::string& counterexample,
                                               SMTSolverInterface* solver) const {
    std::vector<std::string> q = loopContext(lasso);
    q.push_back(counterexample);
    return provenUnsat(solver, q);
}

// ============================================================================
// SUPPORTING INVARIANTS
// ============================================================================

// Initiation : SI(honda) doit valoir pour tout état honda atteignable.
//   - avec stem : stem(x0, x1) ∧ ¬SI(x1) UNSAT, x1 = out-vars du stem ;
//   - sans stem : l'état honda est arbitraire, donc ¬SI(x) UNSAT sur les
//     in-vars du loop.
bool RankingAndInvariantValidator::checkSIInitiation(
    const SupportingInvariant& si, const LassoProgram& lasso,
    SMTSolverInterface* solver, std::string& why)
{
    const bool no_stem = lasso.hasNoStem();
    const auto& honda_ssa = no_stem ? lasso.loop.var_to_ssa_in : lasso.stem.var_to_ssa_out;
    LinExpr t;
    if (!linearTerm(lasso, si.coefficients, si.constant, honda_ssa, t, why)) return false;
    std::vector<std::string> q;
    if (!no_stem) q.push_back(transitionFormula(lasso.stem));
    q.push_back(atom(si.is_strict ? "<=" : "<", t));
    if (!provenUnsat(solver, q)) {
        why = no_stem ? "no stem: SI must hold in every state, and does not"
                      : "stem does not imply SI";
        return false;
    }
    return true;
}

// Consécution : SI(x) ∧ loop(x, x') ∧ ¬SI(x') UNSAT.
bool RankingAndInvariantValidator::checkSIConsecution(
    const SupportingInvariant& si, const LassoProgram& lasso,
    SMTSolverInterface* solver, std::string& why)
{
    LinExpr tin, tout;
    if (!linearTerm(lasso, si.coefficients, si.constant, lasso.loop.var_to_ssa_in, tin, why) ||
        !linearTerm(lasso, si.coefficients, si.constant, lasso.loop.var_to_ssa_out, tout, why))
        return false;
    const std::vector<std::string> q{
        atom(si.is_strict ? ">" : ">=", tin),
        transitionFormula(lasso.loop),
        atom(si.is_strict ? "<=" : "<", tout)};
    if (!provenUnsat(solver, q)) {
        why = "SI is not inductive over the loop";
        return false;
    }
    return true;
}

RankingAndInvariantValidator::SIValidationResult RankingAndInvariantValidator::validateSingleSI(
    int si_index,
    const SupportingInvariant& si,
    const LassoProgram& lasso,
    SMTSolverInterface* solver)
{
    SIValidationResult result;
    result.si_index = si_index;
    result.is_valid = false;
    result.initiation_check = false;
    result.consecution_check = false;

    std::string why;
    result.initiation_check = checkSIInitiation(si, lasso, solver, why);
    if (!result.initiation_check) {
        result.error_message = "initiation failed: " + why;
        return result;
    }
    result.consecution_check = checkSIConsecution(si, lasso, solver, why);
    if (!result.consecution_check) {
        result.error_message = "consecution failed: " + why;
        return result;
    }
    result.is_valid = true;
    return result;
}

// Remplit valid_sis avec les SI prouvés (initiation + consécution). Un SI non
// prouvé est seulement écarté : les obligations du ranking sont alors
// vérifiées sans lui, ce qui reste sain.
bool RankingAndInvariantValidator::validateAllSupportingInvariants(
    const std::vector<SupportingInvariant>& sis,
    const LassoProgram& lasso,
    SMTSolverInterface* solver,
    std::vector<SIValidationResult>& si_results_out)
{
    const bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    valid_sis.clear();
    si_results_out.clear();
    bool all_valid = true;
    for (size_t i = 0; i < sis.size(); ++i) {
        SIValidationResult r = validateSingleSI(static_cast<int>(i), sis[i], lasso, solver);
        si_results_out.push_back(r);
        if (r.is_valid) valid_sis.push_back(sis[i]);
        else all_valid = false;
        if (verbose)
            std::cout << "  SI #" << i << ": "
                      << (r.is_valid ? "valid" : "not proven, ignored (" + r.error_message + ")")
                      << std::endl;
    }
    return all_valid;
}

// ============================================================================
// RANKING FUNCTIONS
// ============================================================================

// Préambule commun à tous les templates : forme, deltas, contexte du solver,
// SI, termes. Renvoie false (avec res.error_message) si l'argument est déjà
// rejeté.
bool RankingAndInvariantValidator::prepare(
    const TerminationArgument& argument, const LassoProgram& lasso,
    SMTSolverInterface* solver, const char* template_name,
    ValidationResult& res, LoopTerms& terms)
{
    res.is_valid = false;
    res.rf_bounded_check = false;
    res.rf_decreasing_check = false;
    res.all_si_valid = false;
    const auto& C = argument.ranking_functions;
    if (C.empty()) {
        res.error_message = std::string(template_name) + ": no component";
        return false;
    }
    if (!allDeltasPositive(C)) {
        res.error_message = std::string(template_name) + ": a delta is not > 0";
        return false;
    }
    registerProgramVariablesToSolver(solver, lasso);
    res.all_si_valid = validateAllSupportingInvariants(argument.supporting_invariants, lasso, solver, res.si_results);
    std::string why;
    if (!buildLoopTerms(lasso, C, terms, why)) {
        res.error_message = why;
        return false;
    }
    return true;
}

// ============================================================================
// AFFINE  --  δ > 0,  f(x) >= 0,  f(x) - f(x') >= δ
// ============================================================================

RankingAndInvariantValidator::ValidationResult RankingAndInvariantValidator::validate(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    SMTSolverInterface* solver)
{
    ValidationResult res;
    LoopTerms T;
    if (argument.ranking_functions.size() != 1) {
        res.is_valid = res.rf_bounded_check = res.rf_decreasing_check = false;
        res.all_si_valid = false;
        res.error_message = "Affine: expected exactly one component, got "
                            + std::to_string(argument.ranking_functions.size());
        return res;
    }
    if (!prepare(argument, lasso, solver, "Affine", res, T)) return res;
    const Rational& delta = argument.ranking_functions[0].delta;

    res.rf_bounded_check = holdsOnLoop(lasso, atom("<", T.in[0]), solver);
    if (!res.rf_bounded_check) { res.error_message = "f(x) >= 0 not proven on the loop"; return res; }

    res.rf_decreasing_check = holdsOnLoop(lasso, atom("<", T.in[0] - T.out[0] - delta), solver);
    if (!res.rf_decreasing_check) { res.error_message = "f(x) - f(x') >= delta not proven on the loop"; return res; }

    res.is_valid = true;
    return res;
}

// ============================================================================
// NESTED  --  δ > 0,  f0(x) - f0(x') >= δ,
//             fi(x) - fi(x') + f_{i-1}(x) >= 0 (i > 0),  f_{k-1}(x) >= 0
// ============================================================================

RankingAndInvariantValidator::NestedValidationResult RankingAndInvariantValidator::validateNested(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    SMTSolverInterface* solver)
{
    NestedValidationResult result;
    result.is_valid = false;
    result.all_si_valid = false;
    result.last_component_bounded_check = false;

    ValidationResult pre;
    LoopTerms T;
    const bool prepared = prepare(argument, lasso, solver, "Nested", pre, T);
    result.all_si_valid = pre.all_si_valid;
    result.si_results = pre.si_results;
    const auto& C = argument.ranking_functions;
    for (int i = 0; i < static_cast<int>(C.size()); ++i)
        result.component_results.push_back({i, false});
    if (!prepared) { result.error_message = pre.error_message; return result; }

    const int k = static_cast<int>(C.size());
    const Rational& delta = C[0].delta;
    for (int i = 0; i < k; ++i) {
        const std::string ce = (i == 0)
            ? atom("<", T.in[0] - T.out[0] - delta)
            : atom("<", T.in[i] - T.out[i] + T.in[i - 1]);
        result.component_results[i].nested_decrease_check = holdsOnLoop(lasso, ce, solver);
        if (!result.component_results[i].nested_decrease_check) {
            result.error_message = "nested decrease not proven for component " + std::to_string(i);
            return result;
        }
    }
    result.last_component_bounded_check = holdsOnLoop(lasso, atom("<", T.in[k - 1]), solver);
    if (!result.last_component_bounded_check) {
        result.error_message = "f_{k-1}(x) >= 0 not proven on the loop";
        return result;
    }
    result.is_valid = true;
    return result;
}

// ============================================================================
// LEXICOGRAPHIC  --  δi > 0,  fi(x) > 0 (tout i),
//   consec_i (i < k-1) : fi(x') <= fi(x)  ∨  ∃ j < i : fj(x) - fj(x') > δj,
//   decrement          : ∃ i : fi(x) - fi(x') > δi
// ============================================================================

RankingAndInvariantValidator::ValidationResult RankingAndInvariantValidator::validateLexicographic(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    SMTSolverInterface* solver)
{
    ValidationResult res;
    LoopTerms T;
    if (!prepare(argument, lasso, solver, "Lexicographic", res, T)) return res;
    const auto& C = argument.ranking_functions;
    const int k = static_cast<int>(C.size());
    auto notDecreased = [&](int j) {
        return atom("<=", T.in[j] - T.out[j] - C[j].delta);
    };

    res.rf_bounded_check = true;
    for (int i = 0; i < k && res.rf_bounded_check; ++i)
        res.rf_bounded_check = holdsOnLoop(lasso, atom("<=", T.in[i]), solver);
    if (!res.rf_bounded_check) { res.error_message = "lexicographic bound fi(x) > 0 not proven"; return res; }

    res.rf_decreasing_check = true;
    for (int i = 0; i + 1 < k && res.rf_decreasing_check; ++i) {
        std::vector<std::string> ce{atom(">", T.out[i] - T.in[i])};
        for (int j = 0; j < i; ++j) ce.push_back(notDecreased(j));
        res.rf_decreasing_check = holdsOnLoop(lasso, smtAnd(ce), solver);
    }
    if (res.rf_decreasing_check) {
        std::vector<std::string> ce;
        for (int i = 0; i < k; ++i) ce.push_back(notDecreased(i));
        res.rf_decreasing_check = holdsOnLoop(lasso, smtAnd(ce), solver);
    }
    if (!res.rf_decreasing_check) { res.error_message = "lexicographic consecution/decrement not proven"; return res; }

    res.is_valid = true;
    return res;
}

// ============================================================================
// MULTIPHASE  --  δi > 0,  f0(x) - f0(x') > δ0,
//   fi(x) - fi(x') > δi  ∨  f_{i-1}(x) > 0  (i > 0),   ∨_i fi(x) > 0
// ============================================================================

RankingAndInvariantValidator::ValidationResult RankingAndInvariantValidator::validateMultiphase(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    SMTSolverInterface* solver)
{
    ValidationResult res;
    LoopTerms T;
    if (!prepare(argument, lasso, solver, "Multiphase", res, T)) return res;
    const auto& C = argument.ranking_functions;
    const int k = static_cast<int>(C.size());

    res.rf_decreasing_check = true;
    for (int i = 0; i < k && res.rf_decreasing_check; ++i) {
        const std::string not_decr = atom("<=", T.in[i] - T.out[i] - C[i].delta);
        const std::string ce = (i == 0) ? not_decr : smtAnd({not_decr, atom("<=", T.in[i - 1])});
        res.rf_decreasing_check = holdsOnLoop(lasso, ce, solver);
    }
    if (!res.rf_decreasing_check) { res.error_message = "multiphase decrease not proven"; return res; }

    std::vector<std::string> ce;
    for (int i = 0; i < k; ++i) ce.push_back(atom("<=", T.in[i]));
    res.rf_bounded_check = holdsOnLoop(lasso, smtAnd(ce), solver);
    if (!res.rf_bounded_check) { res.error_message = "multiphase bound (some fi(x) > 0) not proven"; return res; }

    res.is_valid = true;
    return res;
}

// ============================================================================
// PIECEWISE  --  morceau i actif en x ssi h_i(x) >= 0 ; δi > 0,
//   bound_i      : h_i(x) >= 0  ⇒  f_i(x) >= 0,
//   decrease_i,j : h_i(x) >= 0 ∧ h_j(x') >= 0  ⇒  f_i(x) - f_j(x') >= δi,
//   exhaustive   : ∨_i h_i(x) >= 0.
// La décroissance porte sur toute paire (i, j), comme dans Ultimate
// (PiecewiseTemplate). Une garde par morceau, sinon rejet.
// ============================================================================

RankingAndInvariantValidator::ValidationResult RankingAndInvariantValidator::validatePiecewise(
    const TerminationArgument& argument,
    const LassoProgram& lasso,
    SMTSolverInterface* solver)
{
    ValidationResult res;
    const auto& C = argument.ranking_functions;
    const auto& H = argument.guards;
    if (H.size() != C.size()) {
        res.is_valid = res.rf_bounded_check = res.rf_decreasing_check = false;
        res.all_si_valid = false;
        res.error_message = "Piecewise: " + std::to_string(H.size()) + " guards for "
                            + std::to_string(C.size()) + " pieces";
        return res;
    }
    LoopTerms T;
    if (!prepare(argument, lasso, solver, "Piecewise", res, T)) return res;
    LoopTerms G;
    std::string why;
    if (!buildLoopTerms(lasso, H, G, why)) { res.error_message = why; return res; }
    const int k = static_cast<int>(C.size());

    res.rf_bounded_check = true;
    for (int i = 0; i < k && res.rf_bounded_check; ++i)
        res.rf_bounded_check = holdsOnLoop(
            lasso, smtAnd({atom(">=", G.in[i]), atom("<", T.in[i])}), solver);
    if (!res.rf_bounded_check) { res.error_message = "piecewise bound (h_i >= 0 => f_i >= 0) not proven"; return res; }

    res.rf_decreasing_check = true;
    for (int i = 0; i < k && res.rf_decreasing_check; ++i)
        for (int j = 0; j < k && res.rf_decreasing_check; ++j)
            res.rf_decreasing_check = holdsOnLoop(lasso, smtAnd({
                atom(">=", G.in[i]),
                atom(">=", G.out[j]),
                atom("<", T.in[i] - T.out[j] - C[i].delta)}), solver);
    if (!res.rf_decreasing_check) { res.error_message = "piecewise decrease across pieces not proven"; return res; }

    std::vector<std::string> ce;
    for (int i = 0; i < k; ++i) ce.push_back(atom("<", G.in[i]));
    res.rf_decreasing_check = holdsOnLoop(lasso, smtAnd(ce), solver);
    if (!res.rf_decreasing_check) { res.error_message = "piecewise exhaustiveness (some h_i >= 0) not proven"; return res; }

    res.is_valid = true;
    return res;
}

// ============================================================================
// AFFICHAGE
// ============================================================================

void RankingAndInvariantValidator::printValidationResult(const ValidationResult& result) const
{
    std::cout << "\n  Validation : " << (result.is_valid ? "VALID" : "INVALID") << std::endl;
    std::cout << "    bounded=" << (result.rf_bounded_check ? "OK" : "FAIL")
              << "  decreasing=" << (result.rf_decreasing_check ? "OK" : "FAIL")
              << "  SI=" << (result.all_si_valid ? "OK" : "FAIL") << std::endl;
    for (const auto& si_res : result.si_results) {
        std::cout << "    SI #" << si_res.si_index << " : " << (si_res.is_valid ? "OK" : "FAIL");
        if (!si_res.is_valid) std::cout << " (" << si_res.error_message << ")";
        std::cout << std::endl;
    }
    if (!result.is_valid && !result.error_message.empty())
        std::cout << "    Error: " << result.error_message << std::endl;
}

void RankingAndInvariantValidator::printNestedValidationResult(
    const NestedValidationResult& result) const
{
    std::cout << "\n  Nested validation : " << (result.is_valid ? "VALID" : "INVALID") << std::endl;
    std::cout << "    SI=" << (result.all_si_valid ? "OK" : "FAIL") << std::endl;
    for (const auto& si_res : result.si_results) {
        std::cout << "    SI #" << si_res.si_index << " : " << (si_res.is_valid ? "OK" : "FAIL");
        if (!si_res.is_valid) std::cout << " (" << si_res.error_message << ")";
        std::cout << std::endl;
    }
    for (const auto& cr : result.component_results)
        std::cout << "    f" << cr.index
                  << "  decrease=" << (cr.nested_decrease_check ? "OK" : "FAIL") << std::endl;
    std::cout << "    bounded (last component)=" << (result.last_component_bounded_check ? "OK" : "FAIL") << std::endl;
    if (!result.is_valid && !result.error_message.empty())
        std::cout << "    Error: " << result.error_message << std::endl;
}
