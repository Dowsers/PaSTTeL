#include <iostream>
#include <cassert>

#include "termination/ranking_based_technique.h"
#include "termination/ranking_and_invariant_validator.h"
#include "templates/affine_template.h"
#include "templates/nested_template.h"
#include "templates/lexicographic_template.h"
#include "templates/multiphase_template.h"
#include "templates/piecewise_template.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;
extern bool USE_RF_VALIDATOR;   // opt-in (-val): re-check the synthesized RF

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

RankingBasedTechnique::RankingBasedTechnique(
    SMTSolverInterface* solver,
    const std::string& template_name,
    const std::vector<TemplateConfig>& configs,
    int num_components,
    int max_components)
        : template_name_(template_name)
        , configs_(configs)
        , num_components_(num_components)
        , max_components_(max_components)
        , cancelled_(false)
        , last_synthesizer_(nullptr)
{
    solver_ = solver;
}

// ============================================================================
// INITIALISATION
// ============================================================================

void RankingBasedTechnique::init(const LassoProgram& lasso) {
    lasso_ = lasso;
    cancelled_.store(false);
    lasso_ = lasso_.linearize(&cancelled_);
    lasso_.declareSolverContext(solver_, true);
}

// ============================================================================
// VALIDATION
// ============================================================================

bool RankingBasedTechnique::validateConfiguration() const {
    if (template_name_.empty()) {
        std::cerr << "Error: No template configured" << std::endl;
        return false;
    }
    if (configs_.empty()) {
        std::cerr << "Error: No configurations provided" << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// CRÉATION DE TEMPLATES
// ============================================================================

RankingTemplate* RankingBasedTechnique::createTemplate(
    const std::string& template_name,
    int /*num_si_strict*/,
    int /*num_si_nonstrict*/,
    int num_components) const {

    // Les SI sont maintenant gérés par SupportingInvariantGenerator dans le synthesizer.
    // Les templates ne reçoivent plus les counts SI.
    if (template_name == "AffineTemplate") {
        return new AffineTemplate();
    } else if (template_name == "NestedTemplate") {
        return new NestedTemplate(num_components);
    } else if (template_name == "LexicographicTemplate") {
        return new LexicographicTemplate(num_components);
    } else if (template_name == "MultiphaseTemplate") {
        return new MultiphaseTemplate(num_components);
    } else if (template_name == "PiecewiseTemplate") {
        return new PiecewiseTemplate(num_components);
    } else {
        throw std::invalid_argument("Unknown template name: " + template_name);
    }
}

// ============================================================================
// ANALYSE PRINCIPALE
// ============================================================================

AnalysisResult RankingBasedTechnique::analyze() {
    ProofCertificate proof;
    proof.technique_name = getName();

    if (!validateConfiguration()) {
        proof.description = "Invalid configuration";
        proof_ = proof;
        return proof_.status;
    }

    bool verbosity = (VERBOSITY == VerbosityLevel::VERBOSE);

    bool is_nested = (template_name_ == "NestedTemplate" || template_name_ == "LexicographicTemplate"
                    || template_name_ == "MultiphaseTemplate" || template_name_ == "PiecewiseTemplate");
    int nc_min = (num_components_ > 0) ? num_components_ : 1;
    int nc_max = is_nested ? max_components_ : nc_min;

    for (int nc = nc_min; nc <= nc_max && !cancelled_.load(); nc++) {
        for (const auto& config : configs_) {
            if (cancelled_.load()) {
                if (verbosity)
                    std::cout << "\n[RankingBased] Technique cancelled" << std::endl;
                break;
            }
            solver_->reset();
            bool found = tryTemplateConfiguration(template_name_, config, nc, verbosity);

            if (found) {
                proof.status = AnalysisResult::TERMINATING;
                proof.description = "Termination proof found with " + template_name_ + config.description;

                assert(last_synthesizer_ && "tryTemplateConfiguration returned true but last_synthesizer_ is null");
                const auto& rankfunctions_comp = last_synthesizer_->getTerminationArgument().ranking_functions;
                proof.proof_details = "";
                bool multi_component = rankfunctions_comp.size() > 1;
                for (size_t i = 0; i < rankfunctions_comp.size(); ++i) {
                    const auto& rf = rankfunctions_comp[i];
                    std::string label = multi_component ? ("f" + std::to_string(i) + "(x) = ") : "f(x) = ";
                    std::string body = rf.toString();
                    if (body.empty()) body = "0";
                    proof.proof_details += label + body
                        + "  [delta" + (multi_component ? std::to_string(i) : "") + " = " + rf.delta.toString() + "]\n";
                }

                // Surface the Supporting Invariants that actually made this
                // proof go through -- already fully computed by the
                // synthesizer at this point (see GenericTerminationSynthesizer
                // ::extractResults()), previously only ever printed to
                // verbose console output. Variable names go through
                // lasso_.prettyVarName() so a promoted array cell prints as
                // "A[i]" instead of its raw internal name.
                const auto& sis = last_synthesizer_->getTerminationArgument().supporting_invariants;
                if (!sis.empty()) {
                    auto display_name = [this](const std::string& var) {
                        return lasso_.prettyVarName(var);
                    };
                    proof.proof_details += "Supporting invariants:\n";
                    for (size_t i = 0; i < sis.size(); ++i) {
                        proof.proof_details += "  [" + std::to_string(i) + "] "
                            + sis[i].toString(lasso_.program_vars, display_name)
                            + " " + (sis[i].is_strict ? ">" : ">=") + " 0\n";
                    }
                }

                proof.rf_witness = rankfunctions_comp[0].coefficients;
                proof_ = proof;
                return proof_.status;
            }
        }
    }
    proof.description = "No termination proof found";
    proof_ = proof;
    return proof_.status;
}

// ============================================================================
// ESSAI D'UNE COMBINAISON TEMPLATE + CONFIGURATION
// ============================================================================

bool RankingBasedTechnique::tryTemplateConfiguration(
    const std::string& template_name,
    const TemplateConfig& config,
    int num_components,
    int verbosity) {

    if (verbosity) {
        std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cout << "Trying: " << template_name << config.description;
        if (num_components > 0)
            std::cout << " (components=" << num_components << ")";
        std::cout << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    }

    // Créer le template
    RankingTemplate* ranking_template = createTemplate(
        template_name, config.num_si_strict, config.num_si_nonstrict, num_components);

    // Créer le synthesizer — les SI sont gérés par SIG à l'intérieur
    auto synthesizer = std::make_unique<GenericTerminationSynthesizer>(
        lasso_, ranking_template, std::move(solver_),
        config.num_si_strict, config.num_si_nonstrict);

    // Lancer la synthèse
    auto synthesis_result = synthesizer->synthesize();

    if (!synthesis_result.is_valid) {
        if (verbosity) {
            std::cout << "\nNo ranking function found with template "
                    << template_name << config.description << std::endl;
        }
        return false;  // Échec
    }

    if (verbosity) {
        std::cout << "\nFound a ranking function with template "
                << template_name << config.description << std::endl;
        
        synthesizer->printResults(synthesis_result);
    }
    // VALIDATION POST-SYNTHÈSE

    if (USE_RF_VALIDATOR) {
        // The synthesis assertions (template params, Motzkin, SIGs) still sit on
        // the solver; drop them so the certificate is re-checked on a clean
        // context. The termination argument was already materialized during
        // synthesize(), so we no longer need the solver's synthesis state.
        solver_->reset();

        RankingAndInvariantValidator validator;
        const TerminationArgument arg = synthesizer->getTerminationArgument();
        bool ok = true;
        std::string err;

        if (template_name == "AffineTemplate") {
            auto r = validator.validate(arg, lasso_, solver_);
            ok = r.is_valid; err = r.error_message;
            if (verbosity && ok) validator.printValidationResult(r);
        } else if (template_name == "NestedTemplate") {
            auto r = validator.validateNested(arg, lasso_, solver_);
            ok = r.is_valid; err = r.error_message;
            if (verbosity && ok) validator.printNestedValidationResult(r);
        } else if (template_name == "LexicographicTemplate") {
            auto r = validator.validateLexicographic(arg, lasso_, solver_);
            ok = r.is_valid; err = r.error_message;
            if (verbosity && ok) validator.printValidationResult(r);
        } else if (template_name == "MultiphaseTemplate") {
            auto r = validator.validateMultiphase(arg, lasso_, solver_);
            ok = r.is_valid; err = r.error_message;
            if (verbosity && ok) validator.printValidationResult(r);
        } else if (template_name == "PiecewiseTemplate") {
            auto r = validator.validatePiecewise(arg, lasso_, solver_);
            ok = r.is_valid; err = r.error_message;
            if (verbosity && ok) validator.printValidationResult(r);
        } else {
            // Unknown template: no validator available — do not silently accept.
            ok = false; err = "no validator for template " + template_name;
        }

        if (!ok) {
            if (verbosity)
                std::cout << "\n[validator] " << template_name
                          << " ranking function REJECTED: " << err << std::endl;
            return false;
        }
    }

    // Succès ! Sauvegarder le résultat
    last_synthesis_result_ = synthesis_result;
    last_synthesizer_ = std::move(synthesizer);

    return true;
}

// ============================================================================
// ANNULATION
// ============================================================================


void RankingBasedTechnique::cancel() {
    cancelled_.store(true);
    if (solver_)
        solver_->interrupt();
}

// ============================================================================
// MÉTADONNÉES
// ============================================================================

std::string RankingBasedTechnique::getName() const {
    bool is_nested = (template_name_ == "NestedTemplate" || template_name_ == "LexicographicTemplate"
                    || template_name_ == "MultiphaseTemplate" || template_name_ == "PiecewiseTemplate");
    if (is_nested && num_components_ > 0) {
        if (max_components_ > num_components_)
            return "RankingBased(" + std::to_string(num_components_) + "-" + std::to_string(max_components_) + "-" + template_name_ + ")";
        return "RankingBased(" + std::to_string(num_components_) + "-" + template_name_ + ")";
    }
    return "RankingBased(" + template_name_ + ")";
}

std::string RankingBasedTechnique::getDescription() const {
    return "Ranking function synthesis with " + template_name_ + " template";
}

void RankingBasedTechnique::printInfo() const {
    std::cout << "Technique: " << getName() << "\n"
            << "Description: " << getDescription() << "\n"
            << "Template: " << template_name_ << "\n"
            << "Configurations: " << configs_.size() << std::endl;
}

// ============================================================================
// AFFICHAGE DES RÉSULTATS
// ============================================================================

void RankingBasedTechnique::printResult(const TerminationResult& result) const {
    if (!result.is_terminating) {
        std::cout << "\n[RankingBased] No termination proof found" << std::endl;
        return;
    }

    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║       TERMINATION PROOF (Ranking-Based)                    ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\n  Status: TERMINATING" << std::endl;
    std::cout << "  Method: " << result.description << std::endl;

    if (!result.witness.empty()) {
        std::cout << "\n  Ranking function coefficients:" << std::endl;
        for (const auto& [var, coef] : result.witness) {
            std::cout << "    • " << var << " : " << coef << std::endl;
        }
    }

    std::cout << std::endl;
}
