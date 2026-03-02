#include <iostream>

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
    , cancelled_(false) {
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

TerminationResult RankingBasedTechnique::analyze(std::shared_ptr<SMTSolver> solver) {
    if (!validateConfiguration()) {
        return TerminationResult();
    }

    solver_ = solver;

    // Récupérer le niveau de verbosité depuis les variables globales
    extern VerbosityLevel VERBOSITY;
    bool verbosity = (VERBOSITY == VerbosityLevel::VERBOSE);

    // Boucle: essayer toutes les configurations pour ce template
    for (const auto& config : configs_) {
        // Vérifier si annulé
        if (cancelled_.load()) {
            if (verbosity) {
                std::cout << "\n[RankingBased] Technique cancelled" << std::endl;
            }
            break;
        }
        solver->reset();  // Réinitialiser le solver avant chaque essai
        // Essayer cette configuration
        bool found = tryTemplateConfiguration(
            template_name_, config, solver, verbosity);

        if (found) {
            // Succès ! Créer le résultat
            TerminationResult result(
                TerminationResult::Type::RANKING_BASED,
                true,
                "Termination proof found with " + template_name_ + config.description,
                getName()
            );

            // Copier les coefficients de la ranking function comme témoin
            if (last_synthesizer_) {
                const auto& rf = last_synthesizer_->getTerminationArgument().ranking_function;
                result.witness = rf.coefficients;
                if (!result.witness.empty()) {
                    std::ostringstream proof;
                    size_t count = 0;
                    for (const auto& [var, coef] : result.witness) {
                        if (coef == 0) continue; // Ignorer les termes nuls
                        proof << coef << var << (count < rf.coefficients.size() - 1 ? " + " : "");
                        count++;
                    }
                    result.proof_details = proof.str();
                }
            }
            return result;
        }
    }
    // Aucune preuve trouvée
    return TerminationResult();
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
    // VALIDATION POST-SYNTHÈSE (seulement pour AffineTemplate)
    // NestedTemplate/LexicographicTemplate : la correction est garantie par la synthèse SMT
    // (le validator ne supporte pas encore la sémantique lexicographique nested)
    if (template_name == "AffineTemplate") {
        RankingAndInvariantValidator validator;
        auto validation_result = validator.validate(
            synthesizer->getTerminationArgument(),
            *lasso_,
            solver
        );

        if (!validation_result.is_valid) {
            if (verbosity) {
                std::cout << "\nValidation failed!" << std::endl;
            }
            return false;  // Échec de validation
        }

        if (verbosity)
            validator.printValidationResult(validation_result);
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
    solver_->interrupt();  // Interrompre le solveur SMT en cours
    std::cout<<"interruption ranking templates !!"<<std::endl;
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
