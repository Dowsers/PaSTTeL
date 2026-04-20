#include <iostream>
#include <sstream>
#include <cmath>
#include <unordered_set>
#include <atomic>
#include <cstdint>
#include <utility>
#include <numeric>

#include "termination/generic_termination_synthesizer.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// std::gcd ne supporte pas __int128 en C++17 — implémentation locale
static __int128 gcd128(__int128 a, __int128 b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { __int128 t = b; b = a % b; a = t; }
    return a;
}

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

GenericTerminationSynthesizer::GenericTerminationSynthesizer(
    const LassoProgram& lasso,
    RankingTemplate* template_ptr,
    std::shared_ptr<SMTSolver> solver,
    int num_si_strict,
    int num_si_nonstrict)
    : lasso_(lasso)
    , template_(template_ptr)
    , solver_(solver)
    , num_si_strict_(num_si_strict)
    , num_si_nonstrict_(num_si_nonstrict)
    , synthesized_(false)
{
    if (!template_) {
        throw std::runtime_error("GenericTerminationSynthesizer: null template pointer");
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  GENERIC TERMINATION SYNTHESIZER           ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << std::endl;
        std::cout << "  Template: " << template_->getName() << std::endl;
        std::cout << "  Description: " << template_->getDescription() << std::endl;
        std::cout << "  SI strict:     " << num_si_strict << std::endl;
        std::cout << "  SI non-strict: " << num_si_nonstrict << std::endl;
    }
}

// ============================================================================
// SYNTHESE PRINCIPALE
// ============================================================================

GenericTerminationSynthesizer::SynthesisResult GenericTerminationSynthesizer::synthesize() {

    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose) {
        std::cout << "\n┌─────────────────────────────────────────┐" << std::endl;
        std::cout << "│ Starting Synthesis                      │" << std::endl;
        std::cout << "└─────────────────────────────────────────┘" << std::endl;
    }

    SynthesisResult result;
    result.template_name = template_->getName();
    result.description = template_->getDescription();

    try {
        // Etape 1: Initialiser le template
        if (verbose)
            std::cout << "\n[1/4] Initializing template..." << std::endl;
        template_->init(lasso_);
        if (verbose)
            template_->printInfo();

        // Etape 2: Declarer les parametres SMT du template
        if (verbose)
            std::cout << "\n[2/4] Declaring SMT parameters..." << std::endl;
        template_->declareParameters(solver_);

        // Creer les SIGs locaux (un par poly_loop x template_part) et declarer leurs params
        createLocalSIGs();

        // Etape 3: Construire et appliquer les transformations de Motzkin
        if (verbose)
            std::cout << "\n[3/4] Applying Motzkin transformations..." << std::endl;

        // phi3/phi4 : loop /\ SI_premises -> dec/bounded
        auto phi34_contexts = buildPhi34Contexts();
        // phi1/phi2 : stem -> SI(x') >= 0 et SI(x) /\ loop -> SI(x') >= 0
        auto phi12_contexts = buildPhi12Contexts();

        std::vector<RankingTemplate::MotzkinContext> all_contexts;
        all_contexts.insert(all_contexts.end(), phi34_contexts.begin(), phi34_contexts.end());
        all_contexts.insert(all_contexts.end(), phi12_contexts.begin(), phi12_contexts.end());

        if (verbose) {
            std::cout << "  phi34 contexts: " << phi34_contexts.size() << std::endl;
            std::cout << "  phi12 contexts: " << phi12_contexts.size() << std::endl;
            std::cout << "  Total Motzkin contexts: " << all_contexts.size() << std::endl;
        }

        applyMotzkinTransformations(all_contexts);

        // Etape 4: Resoudre avec le solveur SMT
        if (verbose) {
            std::cout << "\n[4/4] Solving with SMT..." << std::endl;
            std::cout << "  Total assertions: " << solver_->getAssertionCount() << std::endl;
        }

        result.is_valid = solver_->checkSat();

        if (result.is_valid) {
            // getSimplifiedAssignment: maximiser les zéros parmi les paramètres.
            // IMPORTANT: on ne zéroe PAS les coefficients de variables RF individuellement.
            // Zéroer un coeff de variable pousse Z3 à compenser avec des ratios
            // irrationnels dans les autres coefficients, produisant une RF invalide sur ℤ.
            // On zéroe uniquement: constante RF, delta, SI params.
            // {
            //     auto params = template_->getParameters();

            //     // Séparer les ranking_params en "coefficients de variables" (à ne pas zéroer)
            //     // et "constantes" (dernière entrée de chaque composante, suffixe "_const").
            //     // Les constantes sont identifiables par le suffixe "_const".
            //     std::vector<std::string> simplifiable_params;
            //     for (const auto& p : params.ranking_params) {
            //         if (p.size() >= 6 && p.substr(p.size() - 6) == "_const")
            //             simplifiable_params.push_back(p);
            //     }
            //     if (!params.delta_param.empty())
            //         simplifiable_params.push_back(params.delta_param);
            //     if (!local_sigs_.empty()) {
            //         auto si_params = local_sigs_.front()->getSIParams();
            //         for (const auto& p : si_params)
            //             simplifiable_params.push_back(p);
            //     }

            //     // Tenter de minimiser delta à sa valeur minimale (delta_value + 1).
            //     // Cela force Z3 à choisir des coefficients RF cohérents avec un petit delta,
            //     // évitant des solutions arbitrairement grandes sous-contraintes.
            //     if (!params.delta_param.empty()) {
            //         solver_->push();
            //         solver_->addAssertion("(= " + params.delta_param
            //             + " " + std::to_string(params.delta_value + 1) + ")");
            //         if (!solver_->checkSat()) {
            //             solver_->pop(); // delta_value+1 impossible, garder la solution courante
            //         } else if (verbose) {
            //             std::cout << "  getSimplifiedAssignment: delta fixed to "
            //                       << (params.delta_value + 1) << std::endl;
            //         }
            //     }

            //     int zeroed = 0;
            //     for (const auto& p : simplifiable_params) {
            //         solver_->push();
            //         solver_->addAssertion("(= " + p + " 0)");
            //         if (solver_->checkSat()) {
            //             ++zeroed;
            //         } else {
            //             solver_->pop();
            //         }
            //     }

            //     if (verbose && zeroed > 0)
            //         std::cout << "  getSimplifiedAssignment: " << zeroed
            //                   << "/" << simplifiable_params.size()
            //                   << " parameters zeroed" << std::endl;
            // }

            auto params = template_->getParameters();
            result.parameters = extractParameters(params);
            if (verbose)
                std::cout << "\n✓ SAT - Termination argument found!" << std::endl;
            extractResults();
            synthesized_ = true;
            last_result_ = result;
        } else {
            if (verbose)
                std::cout << "\n✗ UNSAT - No termination argument exists" << std::endl;
        }

    } catch (const std::exception& e) {
        if (verbose)
            std::cerr << "\n  Error during synthesis: " << e.what() << std::endl;
        result.is_valid = false;
    }

    return result;
}

// ============================================================================
// createLocalSIGs -- un SIG par (poly_loop x template_part)
// ============================================================================

void GenericTerminationSynthesizer::createLocalSIGs() {
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    local_sigs_.clear();

    int num_si = num_si_strict_ + num_si_nonstrict_;
    if (num_si == 0) {
        if (verbose)
            std::cout << "  No SIs requested, skipping SIG creation." << std::endl;
        return;
    }

    int num_loop_polys = static_cast<int>(lasso_.loop.polyhedra.size());

    // Nombre de parties du template : dec_list.size() + 1 (bounded)
    std::vector<std::string> loop_in_vars, loop_out_vars;
    const auto& eff_vars_ = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
    for (const auto& var : eff_vars_) {
        loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
    }
    auto dec_list = template_->getConstraintsDec(loop_in_vars, loop_out_vars);
    int num_template_parts = static_cast<int>(dec_list.size()) + 1; // +1 pour bounded

    // Compteur atomique global pour unicite des noms SMT entre appels (thread-safe)
    static std::atomic<int> instance_counter{0};

    if (verbose) {
        std::cout << "  Creating local SIGs: "
                  << num_loop_polys << " polys x "
                  << num_template_parts << " template parts x "
                  << num_si << " SI(s) = "
                  << (num_loop_polys * num_template_parts) << " SIGs"
                  << std::endl;
    }

    num_template_parts_ = num_template_parts;

    for (int p = 0; p < num_loop_polys; ++p) {
        for (int m = 0; m < num_template_parts; ++m) {
            int id = instance_counter++;
            auto sig = std::make_shared<SupportingInvariantGenerator>(
                num_si_strict_, num_si_nonstrict_, id);
            sig->init(lasso_);
            sig->declareParameters(solver_);
            local_sigs_.push_back(sig);
        }
    }
}

// ============================================================================
// buildPhi34Contexts -- phi3 (dec) et phi4 (bounded) avec SI locaux
// Chaque (poly_loop x template_part) utilise son SIG dedié.
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
GenericTerminationSynthesizer::buildPhi34Contexts() const
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    std::vector<RankingTemplate::MotzkinContext> contexts;

    // Variables SSA de la boucle
    std::vector<std::string> loop_in_vars, loop_out_vars;
    const auto& eff_vars_ = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
    for (const auto& var : eff_vars_) {
        loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
    }

    // Conclusions positives du template (avant negation)
    auto dec_list = template_->getConstraintsDec(loop_in_vars, loop_out_vars);
    LinearInequality bounded = template_->getConstraintsBounded(loop_in_vars);

    // Negation logique des conclusions
    for (auto& dec : dec_list) {
        dec.negate();
        dec.strict = !dec.strict;
        dec.motzkin_coef = LinearInequality::ONE;
    }
    bounded.negate();
    bounded.strict = !bounded.strict;
    bounded.motzkin_coef = LinearInequality::ONE;

    int num_dec = static_cast<int>(dec_list.size());

    bool has_sigs = !local_sigs_.empty();

    // phi3 : un contexte par (dec_part x poly_loop)
    for (int m = 0; m < num_dec; ++m) {
        int p = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            RankingTemplate::MotzkinContext ctx;
            ctx.annotation = "phi3: RF decrement part " + std::to_string(m)
                           + " (poly " + std::to_string(p) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            // Premisses SI locales : SI strict -> ANYTHING, non-strict -> ONE
            // local_sigs_[p * num_template_parts_ + m] : SI dedie a la branche p
            if (has_sigs) {
                const auto& sig = local_sigs_[p * num_template_parts_ + m];
                int num_si = sig->getNumSI();
                for (int k = 0; k < num_si; ++k) {
                    LinearInequality si_p = sig->buildSI(k, loop_in_vars);
                    si_p.strict = sig->isStrict(k);
                    si_p.motzkin_coef = sig->isStrict(k)
                        ? LinearInequality::ANYTHING
                        : LinearInequality::ONE;
                    ctx.constraints.push_back(si_p);
                }
            }

            ctx.constraints.push_back(dec_list[m]);

            if (verbose)
                std::cout << "  [phi34] " << ctx.annotation << std::endl;

            contexts.push_back(ctx);
            p++;
        }
    }

    // phi4 : un contexte par poly_loop (bounded)
    {
        int m_bounded = num_dec; // index de la partie bounded
        int p = 0;
        for (const auto& polyhedron : lasso_.loop.polyhedra) {
            RankingTemplate::MotzkinContext ctx;
            ctx.annotation = "phi4: RF boundedness (poly " + std::to_string(p) + ")";

            for (const auto& ineq : polyhedron) {
                ctx.constraints.push_back(ineq);
            }

            if (has_sigs) {
                const auto& sig = local_sigs_[p * num_template_parts_ + m_bounded];
                int num_si = sig->getNumSI();
                for (int k = 0; k < num_si; ++k) {
                    LinearInequality si_p = sig->buildSI(k, loop_in_vars);
                    si_p.strict = sig->isStrict(k);
                    si_p.motzkin_coef = sig->isStrict(k)
                        ? LinearInequality::ANYTHING
                        : LinearInequality::ONE;
                    ctx.constraints.push_back(si_p);
                }
            }

            ctx.constraints.push_back(bounded);

            if (verbose)
                std::cout << "  [phi34] " << ctx.annotation << std::endl;

            contexts.push_back(ctx);
            p++;
        }
    }

    return contexts;
}

// ============================================================================
// buildPhi12Contexts -- phi1 (stem initiation) et phi2 (loop consecution)
// Un contexte par SIG local. Chaque SIG a ses propres variables SMT.
// ============================================================================

std::vector<RankingTemplate::MotzkinContext>
GenericTerminationSynthesizer::buildPhi12Contexts() const
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);
    std::vector<RankingTemplate::MotzkinContext> contexts;

    if (local_sigs_.empty()) {
        return contexts;
    }

    // Variables SSA
    std::vector<std::string> loop_in_vars, loop_out_vars, stem_out_vars;
    const auto& eff_vars_ = lasso_.loop_vars.empty() ? lasso_.program_vars : lasso_.loop_vars;
    for (const auto& var : eff_vars_) {
        loop_in_vars.push_back(lasso_.loop.getSSAVar(var, false));
        loop_out_vars.push_back(lasso_.loop.getSSAVar(var, true));
        stem_out_vars.push_back(lasso_.stem.getSSAVar(var, true));
    }

    int num_loop_polys = static_cast<int>(lasso_.loop.polyhedra.size());
    int ntp = (num_template_parts_ > 0) ? num_template_parts_ : 1;

    // Pour chaque SIG local : sig_idx = p * ntp + m
    for (int sig_idx = 0; sig_idx < static_cast<int>(local_sigs_.size()); ++sig_idx) {
        const auto& sig = local_sigs_[sig_idx];
        int num_si = sig->getNumSI();
        if (num_si == 0) continue;

        // Branche de boucle associee a ce SIG
        int p = (num_loop_polys > 0) ? (sig_idx / ntp) : 0;

        // ----------------------------------------------------------------
        // phi1 : stem(x,x') -> SI_k(x') >= 0
        // ----------------------------------------------------------------
        for (int k = 0; k < num_si; ++k) {
            int stem_poly_idx = 0;
            for (const auto& polyhedron : lasso_.stem.polyhedra) {
                RankingTemplate::MotzkinContext ctx;
                ctx.annotation = "phi1: SIG" + std::to_string(sig_idx)
                               + " SI_" + std::to_string(k)
                               + " initiation (stem poly " + std::to_string(stem_poly_idx) + ")";

                for (const auto& ineq : polyhedron) {
                    ctx.constraints.push_back(ineq);
                }

                LinearInequality neg_si = sig->buildSI(k, stem_out_vars);
                neg_si.negate();
                neg_si.strict = !sig->isStrict(k);
                neg_si.motzkin_coef = LinearInequality::ONE;
                ctx.constraints.push_back(neg_si);

                if (verbose)
                    std::cout << "  [phi12] " << ctx.annotation << std::endl;

                contexts.push_back(ctx);
                stem_poly_idx++;
            }
        }

        // ----------------------------------------------------------------
        // phi2 : SI_k(x) /\ branch_p(x,x') -> SI_k(x') >= 0
        // Chaque SIG est inductive uniquement pour sa branche p.
        // ----------------------------------------------------------------
        if (p < num_loop_polys) {
            const auto& polyhedron = lasso_.loop.polyhedra[p];
            for (int k = 0; k < num_si; ++k) {
                RankingTemplate::MotzkinContext ctx;
                ctx.annotation = "phi2: SIG" + std::to_string(sig_idx)
                               + " SI_" + std::to_string(k)
                               + " consecution (loop poly " + std::to_string(p) + ")";

                for (const auto& ineq : polyhedron) {
                    ctx.constraints.push_back(ineq);
                }

                LinearInequality si_prem = sig->buildSI(k, loop_in_vars);
                si_prem.strict = sig->isStrict(k);
                si_prem.motzkin_coef = LinearInequality::ANYTHING;
                ctx.constraints.push_back(si_prem);

                LinearInequality neg_si_prime = sig->buildSI(k, loop_out_vars);
                neg_si_prime.negate();
                neg_si_prime.strict = !sig->isStrict(k);
                neg_si_prime.motzkin_coef = LinearInequality::ZERO_AND_ONE;
                ctx.constraints.push_back(neg_si_prime);

                if (verbose)
                    std::cout << "  [phi12] " << ctx.annotation << std::endl;

                contexts.push_back(ctx);
            }
        }
    }

    return contexts;
}

// ============================================================================
// APPLICATION DES TRANSFORMATIONS DE MOTZKIN
// ============================================================================

void GenericTerminationSynthesizer::applyMotzkinTransformations(
    const std::vector<RankingTemplate::MotzkinContext>& contexts)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "  Motzkin contexts: " << contexts.size() << std::endl;

    std::unordered_set<std::string> all_vars;

    for (const auto& [var_prog, ssa_in] : lasso_.loop.var_to_ssa_in) {
        all_vars.insert(ssa_in);
    }
    for (const auto& [var_prog, ssa_out] : lasso_.loop.var_to_ssa_out) {
        all_vars.insert(ssa_out);
    }
    for (const auto& [var_prog, ssa_in] : lasso_.stem.var_to_ssa_in) {
        all_vars.insert(ssa_in);
    }
    for (const auto& [var_prog, ssa_out] : lasso_.stem.var_to_ssa_out) {
        all_vars.insert(ssa_out);
    }

    for (const auto& abs : lasso_.function_abstractions) {
        all_vars.insert(abs.fresh_var);
    }

    if (verbose) {
        std::cout << "\n  First few contexts:" << std::endl;
        int display_count = std::min(6, (int)contexts.size());
        for (int i = 0; i < display_count; ++i) {
            std::cout << "  [" << i << "] " << contexts[i].annotation << std::endl;
            std::cout << "      Constraints: " << contexts[i].constraints.size() << std::endl;
        }
        if (contexts.size() > 6) {
            std::cout << "  ... and " << (contexts.size() - 6) << " more" << std::endl;
        }
    }

    for (const auto& ctx : contexts) {
        MotzkinTransformation motzkin;
        motzkin.addConstraintsToSolver(
            ctx.constraints,
            all_vars,
            solver_,
            ctx.annotation);
    }

    if (verbose)
        std::cout << "  ✓ Applied " << contexts.size() << " Motzkin transformations" << std::endl;
}

// ============================================================================
// EXTRACTION DES RESULTATS
// ============================================================================

std::map<std::string, double> GenericTerminationSynthesizer::extractParameters(
    const RankingTemplate::TemplateParameters& params)
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n┌─ Extracting Solution ─┐" << std::endl;

    std::map<std::string, double> values;
    bool has_non_zero_coeff_param = false;

    for (const auto& param : params.ranking_params) {
        double value = solver_->getValue(param);
        values[param] = value;
        has_non_zero_coeff_param = has_non_zero_coeff_param || (std::abs(value) > 1e-9);
        if (verbose)
            std::cout << "│ " << param << " = " << value << std::endl;
    }

    if (!has_non_zero_coeff_param) {
        return {};
    }

    // Extraire les params SI du premier SIG (representatif)
    if (!local_sigs_.empty()) {
        for (const auto& param : local_sigs_.front()->getSIParams()) {
            double value = solver_->getValue(param);
            values[param] = value;
            if (verbose)
                std::cout << "│ " << param << " = " << value << std::endl;
        }
    }

    if (!params.delta_param.empty()) {
        double value = solver_->getValue(params.delta_param);
        values[params.delta_param] = value;
        if (verbose)
            std::cout << "│ " << params.delta_param << " = " << value << std::endl;
    }

    if (verbose)
        std::cout << "└────────────────────────┘" << std::endl;

    return values;
}

// ========================================================================
// CALCUL DU GCD
// ========================================================================

long long GenericTerminationSynthesizer::gcd(long long a, long long b) const {
    a = std::abs(a);
    b = std::abs(b);
    while (b != 0) {
        long long temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

long long GenericTerminationSynthesizer::computeGCD(
    const std::vector<double>& /*coefficients*/,
    double /*constant*/) const
{
    return 1; // Remplacé par normalizeFromRationals
}

// Normalise une liste de rationnels (num, den) en entiers simplifiés.
// Utilise __int128 pour les calculs intermédiaires afin d'éviter les overflows
// quand Z3 retourne des rationnels avec de grands numérateurs/dénominateurs.
// Retourne le vecteur des numérateurs normalisés (int64_t).
static std::vector<long long> rationalListToIntegers(
    const std::vector<std::pair<int64_t, int64_t>>& rationals)
{
    if (rationals.empty()) return {};

    using i128 = __int128;

    // LCM de tous les dénominateurs (en __int128 pour éviter l'overflow)
    i128 lcm = 1;
    for (const auto& [num, den] : rationals) {
        if (den == 0) continue;
        i128 d = (den < 0) ? -(i128)den : (i128)den;
        i128 l = (lcm < 0) ? -lcm : lcm;
        i128 g = gcd128(l, d);
        lcm = lcm / g * d;
    }
    if (lcm < 0) lcm = -lcm;

    // Multiplier chaque numérateur par lcm/den (en __int128)
    std::vector<i128> wide;
    wide.reserve(rationals.size());
    for (const auto& [num, den] : rationals) {
        i128 d = (den < 0) ? -(i128)den : (i128)den;
        i128 n = (i128)num;
        if (den < 0) n = -n;
        wide.push_back(n * (lcm / d));
    }

    // GCD de tous les entiers non nuls (en __int128)
    i128 g = 0;
    for (i128 v : wide) {
        if (v != 0) g = gcd128(g, v);
    }
    if (g == 0) g = 1;

    // Diviser par le GCD et réduire en int64_t
    // Si le résultat dépasse INT64_MAX, saturer : c'est un signe que Z3
    // a retourné un modèle non minimal (coefficients arbitrairement grands).
    std::vector<long long> integers;
    integers.reserve(wide.size());
    constexpr i128 MAX64 = (i128)INT64_MAX;
    constexpr i128 MIN64 = (i128)INT64_MIN;
    for (i128 v : wide) {
        i128 r = v / g;
        if (r > MAX64) r = MAX64;
        if (r < MIN64) r = MIN64;
        integers.push_back((long long)r);
    }

    return integers;
}

// ============================================================================
// NORMALISATION (plus utilisée : la normalisation est faite dans rationalListToIntegers)
// ============================================================================

void GenericTerminationSynthesizer::normalizeRankingFunction(
    RankingFunction& /*rf*/,
    long long /*gcd_value*/) const
{
    // No-op : coefficients are already int64_t integers after rationalListToIntegers
}

void GenericTerminationSynthesizer::normalizeSupportingInvariant(
    SupportingInvariant& /*si*/,
    long long /*gcd_value*/) const
{
    // No-op : coefficients are already int64_t integers after rationalListToIntegers
}

// ============================================================================
// EXTRACTION DES RESULTATS STRUCTURES
// ============================================================================

void GenericTerminationSynthesizer::extractResults()
{
    bool verbose = (VERBOSITY == VerbosityLevel::VERBOSE);

    if (verbose)
        std::cout << "\n╭─ Extraction des resultats ────────────────╮" << std::endl;

    // template vars = loop_vars (loop.out ∩ loop.in).
    // Les variables hors loop ont des coefficients libres dans Z3 → on les exclut.
    const auto& eff_vars = lasso_.loop_vars.empty()
        ? lasso_.program_vars : lasso_.loop_vars;
    size_t num_vars = eff_vars.size();

    termination_argument_.components =
        template_->extractRankingFunctions(solver_, eff_vars);

    if (verbose)
        std::cout << "  ✓ Composantes extraites : " << termination_argument_.components.size() << std::endl;

    if (verbose)
        std::cout << "\n   Normalisation GCD par composante:" << std::endl;

    {
        auto params = template_->getParameters();
        size_t num_comps = termination_argument_.components.size();
        size_t nv = eff_vars.size();

        if (!params.ranking_params.empty() && num_comps > 0) {
            size_t params_per_comp = params.ranking_params.size() / num_comps;

            for (size_t ci = 0; ci < num_comps; ++ci) {
                auto& rf = termination_argument_.components[ci];
                size_t base = ci * params_per_comp;

                std::vector<std::pair<int64_t, int64_t>> comp_rationals;
                for (size_t i = 0; i < params_per_comp && base + i < params.ranking_params.size(); ++i) {
                    comp_rationals.push_back(solver_->getRationalValue(params.ranking_params[base + i]));
                }
                std::vector<long long> integers = rationalListToIntegers(comp_rationals);

                for (size_t i = 0; i < nv && i < integers.size(); ++i) {
                    rf.coefficients[eff_vars[i]] = integers[i];
                }
                if (nv < integers.size()) {
                    rf.constant = integers[nv];
                }

                if (verbose)
                    std::cout << "    f" << ci << " (normalized) -> " << rf.toString(eff_vars) << std::endl;
            }
        }

        // Delta : valeur double exacte depuis le rationnel Z3.
        if (!params.delta_param.empty()) {
            auto delta_r = solver_->getRationalValue(params.delta_param);
            double delta_val = (delta_r.second != 0)
                ? (double)delta_r.first / (double)delta_r.second
                : (double)delta_r.first;
            for (auto& rf : termination_argument_.components) {
                rf.delta = delta_val;
            }
            if (verbose)
                std::cout << "    delta (normalized) = " << delta_val << std::endl;
        }
    }

    if (!termination_argument_.components.empty()) {
        termination_argument_.ranking_function = termination_argument_.components[0];
    }

    termination_argument_.supporting_invariants.clear();

    // Extraire les SI de chaque SIG local (un par template_part).
    // local_sigs_[p * num_template_parts + m] : on itere sur m (template_parts)
    // en prenant p=0 (premier poly_loop, representatif).
    if (local_sigs_.empty()) {
        if (verbose)
            std::cout << "╰───────────────────────────────────────────╯\n" << std::endl;
        return;
    }

    int num_template_parts_ex = (num_template_parts_ > 0) ? num_template_parts_ : 1;

    int params_per_si = static_cast<int>(num_vars) + 1;

    if (verbose)
        std::cout << "\n   Supporting Invariants (from " << num_template_parts_ex << " template parts):" << std::endl;

    for (int m = 0; m < num_template_parts_ex; ++m) {
        // SIG representatif : poly=0, template_part=m
        int sig_idx = 0 * num_template_parts_ex + m;
        if (sig_idx >= static_cast<int>(local_sigs_.size())) break;
        const auto& sig = local_sigs_[sig_idx];

        int num_si = sig->getNumSI();
        auto si_is_strict = sig->getSIIsStrict();
        auto si_params = sig->getSIParams();

        for (int si_idx = 0; si_idx < num_si; ++si_idx) {
            SupportingInvariant si;
            si.is_strict = (si_idx < static_cast<int>(si_is_strict.size()))
                ? si_is_strict[si_idx] : false;

            int start_idx = si_idx * params_per_si;

            std::vector<std::pair<int64_t, int64_t>> si_rationals;
            for (size_t i = 0; i < num_vars && start_idx + (int)i < (int)si_params.size(); ++i) {
                si_rationals.push_back(solver_->getRationalValue(si_params[start_idx + i]));
            }
            if (start_idx + (int)num_vars < (int)si_params.size()) {
                si_rationals.push_back(solver_->getRationalValue(si_params[start_idx + num_vars]));
            }

            std::vector<long long> si_integers = rationalListToIntegers(si_rationals);

            for (size_t i = 0; i < num_vars && i < si_integers.size(); ++i) {
                si.coefficients[eff_vars[i]] = si_integers[i];
            }
            if (num_vars < si_integers.size()) {
                si.constant = si_integers[num_vars];
            }

            if (verbose) {
                std::cout << "   [part " << m << "] -> " << si.toString(eff_vars);
                std::cout << " " << (si.is_strict ? ">" : ">=") << " 0" << std::endl;
            }
            termination_argument_.supporting_invariants.push_back(si);
        }
    }

    if (verbose)
        std::cout << "╰───────────────────────────────────────────╯\n" << std::endl;
}

// ============================================================================
// ACCESSEURS
// ============================================================================

const TerminationArgument&
GenericTerminationSynthesizer::getTerminationArgument() const {
    if (!synthesized_ || !last_result_.is_valid) {
        throw std::runtime_error("getTerminationArgument() called but synthesis was not successful");
    }
    return termination_argument_;
}

// ============================================================================
// AFFICHAGE DES RESULTATS
// ============================================================================

void GenericTerminationSynthesizer::printResults(const SynthesisResult& result) const {
    if (!result.is_valid) {
        std::cout << "\n❌ No termination argument found (UNSAT)" << std::endl;
        std::cout << "  Template: " << result.template_name << std::endl;
        return;
    }

    std::cout << "\n╔════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TERMINATION ARGUMENT                      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════╝" << std::endl;

    std::cout << "\n  Template: " << result.template_name << std::endl;
    std::cout << "   " << result.description << std::endl;

    if (synthesized_ && !termination_argument_.components.empty()) {
        std::cout << "\n  Ranking Function(s):" << std::endl;
        bool multi = termination_argument_.components.size() > 1;
        for (size_t ci = 0; ci < termination_argument_.components.size(); ++ci) {
            const auto& rf = termination_argument_.components[ci];
            if (multi)
                std::cout << "  f" << ci << "(x) = ";
            else
                std::cout << "  f(x) = ";
            std::cout << rf.toString(lasso_.program_vars) << std::endl;
            std::cout << "  delta" << (multi ? std::to_string(ci) : "") << " = " << rf.delta << std::endl;
        }
    }

    if (synthesized_ && !termination_argument_.supporting_invariants.empty()) {
        std::cout << "\n Supporting Invariants:" << std::endl;
        for (size_t i = 0; i < termination_argument_.supporting_invariants.size(); ++i) {
            const auto& si = termination_argument_.supporting_invariants[i];
            std::cout << "  SI_" << i << ": " << si.toString(lasso_.program_vars);
            std::cout << " " << (si.is_strict ? ">" : ">=") << " 0";
            std::cout << " (" << (si.is_strict ? "strict" : "non-strict") << ")" << std::endl;
        }
    }
}
