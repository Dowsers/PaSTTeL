#include <iostream>
#include <cassert>

#include "termination/ranking_based_technique.h"
#include "termination/ranking_and_invariant_validator.h"
#include "templates/affine_template.h"
#include "templates/nested_template.h"
#include "templates/lexicographic_template.h"
#include "utiles.h"

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

RankingBasedTechnique::RankingBasedTechnique(
    const std::string& template_name,
    const std::vector<TemplateConfig>& configs,
    int num_components_nested)
    : template_name_(template_name)
    , configs_(configs)
    , num_components_nested_(num_components_nested)
    , lasso_(nullptr)
    , cancelled_(false)
    , last_synthesizer_(nullptr) {
    solver_ = nullptr; // Initialisé dans analyze()
}

// ============================================================================
// INITIALISATION
// ============================================================================

void RankingBasedTechnique::init(const LassoProgram& lasso) {
    lasso_ = &lasso;
    cancelled_.store(false);
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
    if (!lasso_) {
        std::cerr << "Error: Technique not initialized (call init() first)" << std::endl;
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
    int /*num_si_nonstrict*/) const {

    // Les SI sont maintenant gérés par SupportingInvariantGenerator dans le synthesizer.
    // Les templates ne reçoivent plus les counts SI.
    if (template_name == "AffineTemplate") {
        return new AffineTemplate();
    } else if (template_name == "NestedTemplate") {
        return new NestedTemplate(num_components_nested_);
    } else if (template_name == "LexicographicTemplate") {
        return new LexicographicTemplate(num_components_nested_);
    } else {
        throw std::invalid_argument("Unknown template name: " + template_name);
    }
}

// ============================================================================
// ANALYSE PRINCIPALE
// ============================================================================

AnalysisResult RankingBasedTechnique::analyze(std::shared_ptr<SMTSolver> solver) {
    ProofCertificate proof;
    proof.technique_name = getName();

    if (!validateConfiguration()) {
        proof.description = "Invalid configuration";
        proof_ = proof;
        return proof_.status;
    }

    solver_ = solver;

    extern VerbosityLevel VERBOSITY;
    bool verbosity = (VERBOSITY == VerbosityLevel::VERBOSE);

    for (const auto& config : configs_) {
        if (cancelled_.load()) {
            if (verbosity)
                std::cout << "\n[RankingBased] Technique cancelled" << std::endl;
            break;
        }
        solver->reset();
        bool found = tryTemplateConfiguration(template_name_, config, solver, verbosity);

        if (found) {
            proof.status = AnalysisResult::TERMINATING;
            proof.description = "Termination proof found with " + template_name_ + config.description;

            assert(last_synthesizer_ && "tryTemplateConfiguration returned true but last_synthesizer_ is null");
            const auto& rf = last_synthesizer_->getTerminationArgument().ranking_function;
            proof.rf_witness = rf.coefficients;
            if (!proof.rf_witness.empty()) {
                std::ostringstream proof_str;
                size_t count = 0;
                for (const auto& [var, coef] : proof.rf_witness) {
                    if (coef == 0) continue;
                    proof_str << coef << var << (count < rf.coefficients.size() - 1 ? " + " : "");
                    count++;
                }
                proof.proof_details = proof_str.str();
            }
            proof_ = proof;
            return proof_.status;
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
    std::shared_ptr<SMTSolver> solver,
    int verbosity) {

    if (verbosity) {
        std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cout << "Trying: " << template_name << config.description << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    }

    // Créer le template
    RankingTemplate* ranking_template = createTemplate(
        template_name, config.num_si_strict, config.num_si_nonstrict);

    // Créer le synthesizer — les SI sont gérés par SIG à l'intérieur
    auto synthesizer = std::make_unique<GenericTerminationSynthesizer>(
        *lasso_, ranking_template, solver,
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
    RankingAndInvariantValidator validator;
    if (template_name == "AffineTemplate") {
        auto validation_result = validator.validate(
            synthesizer->getTerminationArgument(),
            *lasso_,
            solver
        );

        if (!validation_result.is_valid) {
            if (verbosity)
                std::cout << "\nValidation failed!" << std::endl;
            return false;
        }

        if (verbosity)
            validator.printValidationResult(validation_result);

    } else if (template_name == "NestedTemplate") {
        auto validation_result = validator.validateNested(
            synthesizer->getTerminationArgument(),
            *lasso_,
            solver
        );

        if (!validation_result.is_valid) {
            if (verbosity)
                std::cout << "\nNested validation failed: " << validation_result.error_message << std::endl;
            return false;
        }

        if (verbosity)
            validator.printNestedValidationResult(validation_result);
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
