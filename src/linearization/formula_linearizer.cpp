#include <sstream>
#include <iostream>
#include <algorithm>
#include <regex>

#include "linearization/formula_linearizer.h"
#include "linearization/nonlinear_mul_handler.h"
#include "utiles.h"

extern VerbosityLevel VERBOSITY;

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

FormulaLinearizer::FormulaLinearizer()
    : m_counter(0)
{
}

// ============================================================================
// GESTION DES HANDLERS
// ============================================================================

void FormulaLinearizer::addHandler(std::unique_ptr<NonLinearTermHandler> handler) {
    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  [Linearizer] Handler added: " << handler->getName() << std::endl;
    }
    m_handlers.push_back(std::move(handler));
}

bool FormulaLinearizer::hasHandlers() const {
    return !m_handlers.empty();
}

NonLinearTermHandler* FormulaLinearizer::findHandler(const std::string& op) const {
    for (const auto& handler : m_handlers) {
        if (handler->canHandle(op)) {
            return handler.get();
        }
    }
    return nullptr;
}

// ============================================================================
// LINEARISATION PRINCIPALE
// ============================================================================

LinearizationResult FormulaLinearizer::linearize(const std::string& formula) {
    LinearizationResult result;

    if (formula.empty() || formula == "true" || m_handlers.empty()) {
        result.linearized_formula = formula;
        result.was_modified = false;
        return result;
    }

    size_t abstractions_before = m_abstractions.size();

    // Preprocessing : handler preprocessing (store elimination, etc.)
    std::string preprocessed = formula;
    for (const auto& handler : m_handlers) {
        preprocessed = handler->preprocessFormula(preprocessed);
    }

    result.linearized_formula = linearizeExpr(preprocessed);

    // was_modified is true if the formula string changed (includes cache hits)
    result.was_modified = (result.linearized_formula != formula);

    size_t new_abstractions = m_abstractions.size() - abstractions_before;
    if (new_abstractions > 0 && VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "  [Linearizer] " << new_abstractions
                  << " function call(s) abstracted" << std::endl;
    }

    return result;
}

void FormulaLinearizer::storeAbstractionsToLasso(LassoProgram& lasso) {
    auto abstractions = getAbstractions();

    for (auto& abs : abstractions) {
        lasso.function_abstractions.push_back(abs);
    }
}

// ============================================================================
// PARCOURS RECURSIF DE L'EXPRESSION
// ============================================================================

std::string FormulaLinearizer::linearizeExpr(const std::string& expr) {
    std::string trimmed = expr;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    size_t end = trimmed.find_last_not_of(" \t\n\r");

    if (start == std::string::npos) return expr;
    trimmed = trimmed.substr(start, end - start + 1);

    // Atome simple (variable ou constante)
    if (trimmed[0] != '(') {
        return trimmed;
    }

    // S-expression : decouper en sous-expressions
    std::vector<std::string> tokens = splitSExpr(trimmed);
    if (tokens.empty()) return trimmed;

    std::string op = tokens[0];

    // Special case for multiplication: only abstract if both operands are non-numeric
    // (i.e., non-linear multiplication like (* x y), not linear like (* 2 x))
    if (op == "*" && tokens.size() == 3) {
        // First, recursively linearize the arguments
        std::vector<std::string> linearized_args;
        for (size_t i = 1; i < tokens.size(); ++i) {
            linearized_args.push_back(linearizeExpr(tokens[i]));
        }

        // Check if this is a non-linear multiplication
        if (NonLinearMultiplicationHandler::isNonLinearMultiplication(linearized_args)) {
            // Find or create a handler for multiplication
            NonLinearTermHandler* mul_handler = findHandler("*");
            if (mul_handler != nullptr) {
                std::ostringstream rebuilt_call;
                rebuilt_call << "(* " << linearized_args[0] << " " << linearized_args[1] << ")";
                return getOrCreateFreshVar(rebuilt_call.str(), "*", *mul_handler, linearized_args);
            }
        }

        // Linear multiplication: keep as-is (rebuild with linearized args)
        std::ostringstream rebuilt;
        rebuilt << "(* " << linearized_args[0] << " " << linearized_args[1] << ")";
        return rebuilt.str();
    }

    // Demander aux handlers si l'un d'eux gere cet operateur
    NonLinearTermHandler* handler = findHandler(op);
    if (handler != nullptr) {
        // Lineariser recursivement les arguments d'abord
        // (au cas ou un argument contient lui-meme un terme non-lineaire)
        std::vector<std::string> linearized_args;
        std::ostringstream rebuilt_call;
        rebuilt_call << "(" << op;
        for (size_t i = 1; i < tokens.size(); ++i) {
            std::string arg = linearizeExpr(tokens[i]);
            linearized_args.push_back(arg);
            rebuilt_call << " " << arg;
        }
        rebuilt_call << ")";

        return getOrCreateFreshVar(rebuilt_call.str(), op, *handler, linearized_args);
    }

    // Operateur builtin ou expression avec arguments : lineariser recursivement
    if (isBuiltinOperator(op) || tokens.size() > 1) {
        std::ostringstream rebuilt;
        rebuilt << "(" << op;
        for (size_t i = 1; i < tokens.size(); ++i) {
            rebuilt << " " << linearizeExpr(tokens[i]);
        }
        rebuilt << ")";
        return rebuilt.str();
    }

    // Cas restant : expression non reconnue, retourner telle quelle
    return trimmed;
}

// ============================================================================
// GESTION DES VARIABLES FRAICHES
// ============================================================================

std::string FormulaLinearizer::getOrCreateFreshVar(
    const std::string& full_call,
    const std::string& op,
    const NonLinearTermHandler& handler,
    const std::vector<std::string>& args)
{
    // Reutiliser la variable si le meme terme a deja ete vu
    auto it = m_call_to_var.find(full_call);
    if (it != m_call_to_var.end()) {
        return it->second;
    }

    // Creer un nom unique : prefixe + nom_operateur + compteur
    std::string fresh_var = handler.getPrefix() + op + "__" + std::to_string(m_counter++);
    std::string sort = handler.getSort(op, args);

    // Enregistrer l'abstraction
    FunctionAbstraction abstraction;
    abstraction.fresh_var = fresh_var;
    abstraction.original_call = full_call;
    abstraction.function_name = op;
    abstraction.sort = sort;

    m_abstractions.push_back(abstraction);
    m_call_to_var[full_call] = fresh_var;
    m_var_to_call[fresh_var] = full_call;

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "    [Linearizer] " << full_call
                  << "  ->  " << fresh_var << std::endl;
    }

    return fresh_var;
}

// ============================================================================
// ACCESSEURS
// ============================================================================

const std::vector<FunctionAbstraction>& FormulaLinearizer::getAbstractions() const {
    return m_abstractions;
}

const std::map<std::string, std::string>& FormulaLinearizer::getAbstractionMap() const {
    return m_var_to_call;
}

void FormulaLinearizer::reset() {
    m_abstractions.clear();
    m_call_to_var.clear();
    m_var_to_call.clear();
    m_counter = 0;
}

// ============================================================================
// UTILITAIRES
// ============================================================================

// splitSExpr() is now inline in the header, delegating to SExprUtils.

bool FormulaLinearizer::isBuiltinOperator(const std::string& op) {
    static const std::set<std::string> builtins = {
        // Arithmetique
        "+", "-", "*", "/", "mod", "div", "abs",
        // Comparaison
        "=", "<", ">", "<=", ">=", "distinct",
        // Logique
        "and", "or", "not", "=>", "ite", "xor",
        // Quantificateurs
        "forall", "exists", "let",
        // Conversion
        "to_int", "to_real", "is_int"
    };
    return builtins.count(op) > 0;
}

