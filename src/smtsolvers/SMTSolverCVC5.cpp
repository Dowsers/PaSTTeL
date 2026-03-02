#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <mutex>

#include "smtsolvers/SMTSolverCVC5.h"

// Mutex global pour sérialiser la construction et destruction des instances CVC5.
// CVC5 (et ses dépendances libpoly/GMP) utilise de l'état global interne
// qui n'est pas thread-safe lors de la création/destruction concurrente.
static std::mutex g_cvc5_lifecycle_mutex;

// ============================================================================
// CONSTRUCTEUR / DESTRUCTEUR
// ============================================================================

SMTSolverCVC5::SMTSolverCVC5(bool verbose)
    : m_tm()
    , m_solver(m_tm)  // Passer le TermManager au constructeur du Solver
    , m_assertion_count(0)
    , m_verbose(verbose)
{
    std::lock_guard<std::mutex> lock(g_cvc5_lifecycle_mutex);

    if (m_verbose) {
        std::cout << "[CVC5] Initialisation du solveur CVC5" << std::endl;
    }

    // Configuration du solveur pour optimiser les performances
    m_solver.setOption("produce-models", "true");
    m_solver.setLogic("ALL");
}

SMTSolverCVC5::~SMTSolverCVC5() {
    std::lock_guard<std::mutex> lock(g_cvc5_lifecycle_mutex);

    if (m_verbose) {
        std::cout << "[CVC5] Destruction du solveur" << std::endl;
    }
}

// ============================================================================
// PUSH / POP
// ============================================================================

void SMTSolverCVC5::push() {
    m_solver.push();
    m_assertion_stack.push(m_assertion_count);

    if (m_verbose) {
        std::cout << "[CVC5] Push (niveau " << m_assertion_stack.size() << ")" << std::endl;
    }
}

void SMTSolverCVC5::pop() {
    if (m_assertion_stack.empty()) {
        throw std::runtime_error("SMTSolverCVC5::pop() appelé sans push() correspondant");
    }

    m_solver.pop();
    m_assertion_count = m_assertion_stack.top();
    m_assertion_stack.pop();

    if (m_verbose) {
        std::cout << "[CVC5] Pop (niveau " << m_assertion_stack.size() << ")" << std::endl;
    }
}

// ============================================================================
// DÉCLARATION DE VARIABLES
// ============================================================================

void SMTSolverCVC5::declareVariable(const std::string& name, const std::string& type) {
    // Vérifier si la variable existe déjà
    if (variableExists(name)) {
        if (m_verbose) {
            std::cout << "[CVC5] Variable '" << name << "' déjà déclarée" << std::endl;
        }
        return;
    }

    // Créer la variable avec le bon type
    cvc5::Sort sort = getCVC5Sort(type);
    cvc5::Term var = m_tm.mkConst(sort, name);

    // Ajouter au cache
    m_variables.insert(std::make_pair(name, var));

    if (m_verbose) {
        std::cout << "[CVC5] Variable déclarée : " << name << " : " << type << std::endl;
    }
}

// ============================================================================
// DÉCLARATION DE FONCTIONS NON INTERPRÉTÉES
// ============================================================================

void SMTSolverCVC5::declareFunction(const std::string& name, const std::string& signature) {
    // Vérifier si la fonction existe déjà
    if (m_functions.find(name) != m_functions.end()) {
        if (m_verbose) {
            std::cout << "[CVC5] Function '" << name << "' already declared" << std::endl;
        }
        return;
    }

    // Parser la signature: "(Type1 Type2 ...) ReturnType" ou "Type1 ReturnType"
    std::vector<cvc5::Sort> domain_sorts;
    cvc5::Sort range_sort = m_tm.getIntegerSort(); // default

    std::string sig = signature;

    // Cas 1: Signature avec parenthèses "(T1 T2) R"
    if (sig.find('(') != std::string::npos) {
        size_t open = sig.find('(');
        size_t close = sig.find(')');

        if (close == std::string::npos) {
            throw std::runtime_error("Invalid function signature: " + signature);
        }

        // Extraire les types du domaine
        std::string domain_str = sig.substr(open + 1, close - open - 1);
        std::istringstream domain_stream(domain_str);
        std::string type;
        while (domain_stream >> type) {
            domain_sorts.push_back(getCVC5Sort(type));
        }

        // Extraire le type de retour
        std::string range_str = sig.substr(close + 1);
        range_str.erase(0, range_str.find_first_not_of(" \t"));
        if (!range_str.empty()) {
            range_sort = getCVC5Sort(range_str);
        }
    }
    // Cas 2: Signature simple "T1 T2" (domaine=[T1], codomaine=T2)
    else {
        std::istringstream sig_stream(sig);
        std::string type;
        std::vector<std::string> types;
        while (sig_stream >> type) {
            types.push_back(type);
        }

        if (types.size() < 2) {
            throw std::runtime_error("Invalid function signature (need at least domain and range): " + signature);
        }

        // Tous sauf le dernier = domaine
        for (size_t i = 0; i < types.size() - 1; ++i) {
            domain_sorts.push_back(getCVC5Sort(types[i]));
        }
        // Le dernier = codomaine
        range_sort = getCVC5Sort(types.back());
    }

    // Créer le type de fonction
    cvc5::Sort func_sort = m_tm.mkFunctionSort(domain_sorts, range_sort);

    // Créer la déclaration de fonction CVC5
    cvc5::Term func = m_tm.mkConst(func_sort, name);

    // Ajouter au cache
    m_functions[name] = func;

    if (m_verbose) {
        std::cout << "[CVC5] Function declared: " << name << " : " << signature << std::endl;
    }
}

// ============================================================================
// AJOUT D'AXIOMES
// ============================================================================

void SMTSolverCVC5::addAxiom(const std::string& axiom) {
    // Stocker l'axiome pour propagation lors du clonage
    m_axioms.push_back(axiom);

    try {
        // Parser l'axiome SMT-LIB2 et l'ajouter comme assertion
        cvc5::Term axiom_term = parseSmtLib2(axiom);

        // Ajouter au solveur
        m_solver.assertFormula(axiom_term);
        m_assertion_count++;

        if (m_verbose) {
            std::cout << "[CVC5] Axiom added: " << axiom << std::endl;
        }

    } catch (const cvc5::CVC5ApiException& e) {
        std::cerr << "[CVC5 ERROR] Error adding axiom: " << axiom << std::endl;
        std::cerr << "[CVC5 ERROR] " << e.what() << std::endl;
        throw;
    }
}

// ============================================================================
// AJOUT D'ASSERTIONS
// ============================================================================

void SMTSolverCVC5::addAssertion(const std::string& assertion) {
    try {
        // Parser l'assertion SMT-LIB2 et la convertir en terme CVC5
        cvc5::Term constraint = parseSmtLib2(assertion);

        // Ajouter au solveur
        m_solver.assertFormula(constraint);
        m_assertion_count++;

        if (m_verbose) {
            std::cout << "[CVC5] Assertion ajoutée (#" << m_assertion_count << "): "
                    << assertion << std::endl;
        }

    } catch (const cvc5::CVC5ApiException& e) {
        std::cerr << "[CVC5 ERROR] Erreur lors de l'ajout de l'assertion: " << assertion << std::endl;
        std::cerr << "[CVC5 ERROR] " << e.what() << std::endl;
        throw;
    }
}

// ============================================================================
// VÉRIFICATION SAT
// ============================================================================

bool SMTSolverCVC5::checkSat() {
    if (m_verbose) {
        std::cout << "[CVC5] Vérification de la satisfiabilité..." << std::endl;
    }

    cvc5::Result result = m_solver.checkSat();

    bool is_sat = result.isSat();

    if (m_verbose) {
        std::cout << "[CVC5] Résultat: ";
        if (result.isSat()) {
            std::cout << "SAT" << std::endl;
        } else if (result.isUnsat()) {
            std::cout << "UNSAT" << std::endl;
        } else {
            std::cout << "UNKNOWN" << std::endl;
        }
    }

    return is_sat;
}

// ============================================================================
// EXTRACTION DE VALEURS
// ============================================================================

double SMTSolverCVC5::getValue(const std::string& var_name) {
    // Vérifier que la variable existe
    if (!variableExists(var_name)) {
        throw std::runtime_error("SMTSolverCVC5::getValue() : variable '" + var_name + "' non déclarée");
    }

    // Obtenir la variable
    cvc5::Term var = getVariable(var_name);

    // Évaluer la variable
    cvc5::Term value = m_solver.getValue(var);

    // Convertir en double
    double result = 0.0;

    if (value.getSort().isBoolean()) {
        // Cas booléen
        result = value.getBooleanValue() ? 1.0 : 0.0;
    } else if (value.getSort().isInteger()) {
        // Entier
        result = static_cast<double>(std::stoll(value.getIntegerValue()));
    } else if (value.getSort().isReal()) {
        // Réel : extraire numérateur et dénominateur
        std::string real_str = value.getRealValue();

        // Format peut être "num/den" ou "num.dec"
        size_t slash_pos = real_str.find('/');
        if (slash_pos != std::string::npos) {
            // Format rationnel "num/den"
            try {
                double numerator = std::stod(real_str.substr(0, slash_pos));
                double denominator = std::stod(real_str.substr(slash_pos + 1));
                result = numerator / denominator;
            } catch (...) {
                throw std::runtime_error("SMTSolverCVC5::getValue() : impossible de parser le réel " + real_str);
            }
        } else {
            // Format décimal
            try {
                result = std::stod(real_str);
            } catch (...) {
                throw std::runtime_error("SMTSolverCVC5::getValue() : impossible de parser " + real_str);
            }
        }
    } else {
        throw std::runtime_error("SMTSolverCVC5::getValue() : type non supporté pour " + var_name);
    }

    if (m_verbose) {
        std::cout << "[CVC5] " << var_name << " = " << result << std::endl;
    }

    return result;
}

// ============================================================================
// COMPTEUR D'ASSERTIONS
// ============================================================================

size_t SMTSolverCVC5::getAssertionCount() const {
    return m_assertion_count;
}

// ============================================================================
// MÉTHODES UTILITAIRES
// ============================================================================

cvc5::Term SMTSolverCVC5::getVariable(const std::string& name) {
    auto it = m_variables.find(name);
    if (it == m_variables.end()) {
        throw std::runtime_error("Variable '" + name + "' non déclarée");
    }
    return it->second;
}

void SMTSolverCVC5::interrupt() {
    // m_solver.interrupt();
    if (m_verbose) {
        std::cout << "[CVC5] CheckSat interrompu" << std::endl;
    }
}

void SMTSolverCVC5::printStatistics() const {
    std::cout << "\n╔══════════════════════════════════════╗" << std::endl;
    std::cout << "║  CVC5 Solveur - Statistiques        ║" << std::endl;
    std::cout << "╚══════════════════════════════════════╝" << std::endl;
    std::cout << "  Variables déclarées : " << m_variables.size() << std::endl;
    std::cout << "  Assertions ajoutées : " << m_assertion_count << std::endl;
    std::cout << "  Niveau de push/pop  : " << m_assertion_stack.size() << std::endl;
}

void SMTSolverCVC5::reset() {
    m_solver.resetAssertions();
    m_variables.clear();
    m_functions.clear();  // Clear function declarations too
    m_axioms.clear();     // Clear axioms too
    m_assertion_count = 0;
    while (!m_assertion_stack.empty()) {
        m_assertion_stack.pop();
    }

    if (m_verbose) {
        std::cout << "[CVC5] Solveur réinitialisé" << std::endl;
    }
}

bool SMTSolverCVC5::variableExists(const std::string& name) const {
    return m_variables.find(name) != m_variables.end();
}

// ============================================================================
// CLONAGE
// ============================================================================

std::shared_ptr<SMTSolver> SMTSolverCVC5::clone() const {
    // Créer une nouvelle instance avec les mêmes paramètres
    auto cloned = std::make_shared<SMTSolverCVC5>(m_verbose);

    // Copier les variables déclarées
    for (const auto& [name, term] : m_variables) {
        std::string sort_name;
        cvc5::Sort sort = term.getSort();
        if (sort.isInteger()) {
            sort_name = "Int";
        } else if (sort.isReal()) {
            sort_name = "Real";
        } else if (sort.isBoolean()) {
            sort_name = "Bool";
        } else if (sort.isArray()) {
            // Reconstruire "(Array <index_sort> <element_sort>)"
            cvc5::Sort idx = sort.getArrayIndexSort();
            cvc5::Sort elem = sort.getArrayElementSort();
            std::string idx_str = idx.isInteger() ? "Int" : idx.isReal() ? "Real" : idx.toString();
            std::string elem_str = elem.isInteger() ? "Int" : elem.isReal() ? "Real" : elem.toString();
            sort_name = "(Array " + idx_str + " " + elem_str + ")";
        } else {
            sort_name = sort.toString();
        }
        cloned->declareVariable(name, sort_name);
    }

    // Copier les fonctions déclarées
    for (const auto& [name, func] : m_functions) {
        // Reconstruire la signature depuis le terme de fonction
        cvc5::Sort func_sort = func.getSort();

        if (func_sort.isFunction()) {
            std::ostringstream sig;
            size_t arity = func_sort.getFunctionArity();

            if (arity > 0) {
                sig << "(";
                std::vector<cvc5::Sort> domain_sorts = func_sort.getFunctionDomainSorts();
                for (size_t i = 0; i < domain_sorts.size(); ++i) {
                    if (i > 0) sig << " ";
                    sig << domain_sorts[i].toString();
                }
                sig << ") ";
            }
            sig << func_sort.getFunctionCodomainSort().toString();

            cloned->declareFunction(name, sig.str());
        }
    }

    // Copier les axiomes
    for (const auto& axiom : m_axioms) {
        cloned->addAxiom(axiom);
    }

    return cloned;
}

// ============================================================================
// PARSING SMT-LIB2
// ============================================================================

cvc5::Term SMTSolverCVC5::parseSmtLib2(const std::string& smt_string) {
    return parseSmtLib2Manual(smt_string);
}

// Fonction auxiliaire pour tokenizer une S-expression
static std::vector<std::string> tokenize_sexp(const std::string& sexp) {
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < sexp.length(); ++i) {
        char c = sexp[i];

        if (c == '|') {
            // SMT-LIB2 quoted identifier: consume until closing '|'
            // treating all internal characters (including parentheses) as part of the token.
            current += c;
            ++i;
            while (i < sexp.length() && sexp[i] != '|') {
                current += sexp[i];
                ++i;
            }
            if (i < sexp.length()) {
                current += sexp[i]; // closing '|'
            }
        } else if (c == '(' || c == ')') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.push_back(std::string(1, c));
        } else if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

cvc5::Term SMTSolverCVC5::parseSmtLib2Manual(const std::string& smt_string) {
    std::string trimmed = smt_string;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

    // Tokenizer l'expression
    std::vector<std::string> tokens = tokenize_sexp(trimmed);
    size_t pos = 0;

    return parseSexpTokens(tokens, pos);
}

// Parser récursif pour les tokens S-exp
cvc5::Term SMTSolverCVC5::parseSexpTokens(const std::vector<std::string>& tokens, size_t& pos) {
    return parseSexpTokensWithBindings(tokens, pos, {});
}

// Parser avec support pour les variables liées (quantificateurs)
cvc5::Term SMTSolverCVC5::parseSexpTokensWithBindings(
    const std::vector<std::string>& tokens,
    size_t& pos,
    const std::map<std::string, cvc5::Term>& bound_vars) {

    if (pos >= tokens.size()) {
        throw std::runtime_error("Unexpected end of expression");
    }

    const std::string& token = tokens[pos];

    // Cas 1: S-expression (op arg1 arg2 ...)
    if (token == "(") {
        pos++; // Consommer '('

        if (pos >= tokens.size()) {
            throw std::runtime_error("Unexpected end after '('");
        }

        std::string op = tokens[pos++]; // Lire l'opérateur

        // Cas spécial: quantificateurs forall et exists
        if (op == "forall" || op == "exists") {
            return parseQuantifier(op, tokens, pos, bound_vars);
        }

        // Collecter les arguments
        std::vector<cvc5::Term> args;
        while (pos < tokens.size() && tokens[pos] != ")") {
            args.push_back(parseSexpTokensWithBindings(tokens, pos, bound_vars));
        }

        if (pos >= tokens.size() || tokens[pos] != ")") {
            throw std::runtime_error("Missing closing parenthesis");
        }
        pos++; // Consommer ')'

        // Construire le terme selon l'opérateur
        return buildTerm(op, args);
    }

    // Cas 2: Atome (variable liée, variable globale, nombre ou constante)
    pos++;

    // Vérifier d'abord les variables liées (priorité sur les globales)
    if (bound_vars.find(token) != bound_vars.end()) {
        return bound_vars.at(token);
    }

    // Variable globale
    if (variableExists(token)) {
        return getVariable(token);
    }

    // Booléens
    if (token == "true") {
        return m_tm.mkTrue();
    }
    if (token == "false") {
        return m_tm.mkFalse();
    }

    // Nombre
    if (std::isdigit(token[0]) || (token[0] == '-' && token.length() > 1)) {
        if (token.find('.') != std::string::npos) {
            return m_tm.mkReal(token);
        } else {
            return m_tm.mkInteger(token);
        }
    }

    throw std::runtime_error("Unknown atom: " + token);
}

// Parser un quantificateur (forall ou exists)
cvc5::Term SMTSolverCVC5::parseQuantifier(
    const std::string& quantifier,
    const std::vector<std::string>& tokens,
    size_t& pos,
    const std::map<std::string, cvc5::Term>& outer_bound_vars) {

    // Structure: (forall ((var1 Type1) (var2 Type2) ...) body)
    // À ce point, on a déjà lu "(" et "forall"/"exists"

    // On attend "(" pour la liste des variables
    if (pos >= tokens.size() || tokens[pos] != "(") {
        throw std::runtime_error(quantifier + " expects binding list");
    }
    pos++; // Consommer '(' de la liste

    // Parser les bindings ((var1 Type1) (var2 Type2) ...)
    std::vector<cvc5::Term> bound_var_terms;
    std::map<std::string, cvc5::Term> new_bound_vars = outer_bound_vars;

    while (pos < tokens.size() && tokens[pos] == "(") {
        pos++; // Consommer '(' du binding

        if (pos + 1 >= tokens.size()) {
            throw std::runtime_error("Incomplete variable binding");
        }

        std::string var_name = tokens[pos++];
        std::string var_type = tokens[pos++];

        // Consommer ')' du binding
        if (pos >= tokens.size() || tokens[pos] != ")") {
            throw std::runtime_error("Missing ) in variable binding");
        }
        pos++;

        // Créer la variable liée
        cvc5::Sort sort;
        if (var_type == "Int") {
            sort = m_tm.getIntegerSort();
        } else if (var_type == "Real") {
            sort = m_tm.getRealSort();
        } else if (var_type == "Bool") {
            sort = m_tm.getBooleanSort();
        } else {
            sort = m_tm.getIntegerSort(); // Par défaut
        }

        cvc5::Term bound_var = m_tm.mkVar(sort, var_name);
        bound_var_terms.push_back(bound_var);
        new_bound_vars[var_name] = bound_var;
    }

    // Consommer ')' de la liste des bindings
    if (pos >= tokens.size() || tokens[pos] != ")") {
        throw std::runtime_error("Missing ) after binding list");
    }
    pos++;

    // Parser le corps avec les nouvelles variables liées
    cvc5::Term body = parseSexpTokensWithBindings(tokens, pos, new_bound_vars);

    // Consommer ')' du quantificateur
    if (pos >= tokens.size() || tokens[pos] != ")") {
        throw std::runtime_error("Missing ) after " + quantifier + " body");
    }
    pos++;

    // Construire le terme quantifié
    cvc5::Term bound_var_list = m_tm.mkTerm(cvc5::Kind::VARIABLE_LIST, bound_var_terms);

    if (quantifier == "forall") {
        return m_tm.mkTerm(cvc5::Kind::FORALL, {bound_var_list, body});
    } else {
        return m_tm.mkTerm(cvc5::Kind::EXISTS, {bound_var_list, body});
    }
}

// Construire un terme CVC5 à partir d'un opérateur et de ses arguments
cvc5::Term SMTSolverCVC5::buildTerm(const std::string& op, const std::vector<cvc5::Term>& args) {
    using namespace cvc5;

    // Opérateurs arithmétiques
    if (op == "+") {
        if (args.empty()) throw std::runtime_error("+ requires at least 1 argument");
        if (args.size() == 1) return args[0];  // (+ x) = x
        Term result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = m_tm.mkTerm(Kind::ADD, {result, args[i]});
        }
        return result;
    }

    if (op == "-") {
        if (args.empty()) throw std::runtime_error("- requires at least 1 argument");
        if (args.size() == 1) {
            // Négation unaire: (- x) = -x
            return m_tm.mkTerm(Kind::NEG, {args[0]});
        } else if (args.size() == 2) {
            return m_tm.mkTerm(Kind::SUB, {args[0], args[1]});
        } else {
            // (- a b c ...) = a - b - c - ...
            Term result = args[0];
            for (size_t i = 1; i < args.size(); ++i) {
                result = m_tm.mkTerm(Kind::SUB, {result, args[i]});
            }
            return result;
        }
    }

    if (op == "*") {
        if (args.empty()) throw std::runtime_error("* requires at least 1 argument");
        if (args.size() == 1) return args[0];  // (* x) = x
        Term result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = m_tm.mkTerm(Kind::MULT, {result, args[i]});
        }
        return result;
    }

    if (op == "/") {
        if (args.size() != 2) throw std::runtime_error("/ requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::DIVISION, {args[0], args[1]});
    }

    if (op == "div") {
        if (args.size() != 2) throw std::runtime_error("div requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::INTS_DIVISION, {args[0], args[1]});
    }

    if (op == "mod") {
        if (args.size() != 2) throw std::runtime_error("mod requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::INTS_MODULUS, {args[0], args[1]});
    }

    // Opérateurs de comparaison
    if (op == "=") {
        if (args.size() != 2) throw std::runtime_error("= requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::EQUAL, {args[0], args[1]});
    }

    if (op == "<") {
        if (args.size() != 2) throw std::runtime_error("< requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::LT, {args[0], args[1]});
    }

    if (op == "<=") {
        if (args.size() != 2) throw std::runtime_error("<= requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::LEQ, {args[0], args[1]});
    }

    if (op == ">") {
        if (args.size() != 2) throw std::runtime_error("> requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::GT, {args[0], args[1]});
    }

    if (op == ">=") {
        if (args.size() != 2) throw std::runtime_error(">= requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::GEQ, {args[0], args[1]});
    }

    // Opérateurs logiques
    if (op == "and") {
        if (args.empty()) throw std::runtime_error("and requires at least 1 argument");
        if (args.size() == 1) return args[0];  // (and x) = x
        return m_tm.mkTerm(Kind::AND, args);
    }

    if (op == "or") {
        if (args.empty()) throw std::runtime_error("or requires at least 1 argument");
        if (args.size() == 1) return args[0];  // (or x) = x
        return m_tm.mkTerm(Kind::OR, args);
    }

    if (op == "not") {
        if (args.size() != 1) throw std::runtime_error("not requires exactly 1 argument");
        return m_tm.mkTerm(Kind::NOT, {args[0]});
    }

    if (op == "=>") {
        if (args.size() != 2) throw std::runtime_error("=> requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::IMPLIES, {args[0], args[1]});
    }

    if (op == "ite") {
        if (args.size() != 3) throw std::runtime_error("ite requires exactly 3 arguments");
        return m_tm.mkTerm(Kind::ITE, {args[0], args[1], args[2]});
    }

    // Opérateur distinct
    if (op == "distinct") {
        if (args.size() < 2) throw std::runtime_error("distinct requires at least 2 arguments");
        return m_tm.mkTerm(Kind::DISTINCT, args);
    }

    // Opérateurs sur les tableaux (Array)
    if (op == "select") {
        if (args.size() != 2) throw std::runtime_error("select requires exactly 2 arguments");
        return m_tm.mkTerm(Kind::SELECT, {args[0], args[1]});
    }

    if (op == "store") {
        if (args.size() != 3) throw std::runtime_error("store requires exactly 3 arguments");
        return m_tm.mkTerm(Kind::STORE, {args[0], args[1], args[2]});
    }

    // Vérifier si c'est une fonction non-interprétée déclarée
    if (m_functions.find(op) != m_functions.end()) {
        cvc5::Term func = m_functions.at(op);
        std::vector<cvc5::Term> all_args;
        all_args.push_back(func);
        all_args.insert(all_args.end(), args.begin(), args.end());
        return m_tm.mkTerm(Kind::APPLY_UF, all_args);
    }

    throw std::runtime_error("Unknown operator: " + op);
}

// ============================================================================
// CONVERSION DE TYPES
// ============================================================================

cvc5::Sort SMTSolverCVC5::getCVC5Sort(const std::string& type) {
    if (type == "Int") {
        return m_tm.getIntegerSort();
    } else if (type == "Real") {
        return m_tm.getRealSort();
    } else if (type == "Bool") {
        return m_tm.getBooleanSort();
    } else if (type.substr(0, 6) == "(Array") {
        // Parse "(Array <index_sort> <element_sort>)"
        std::string inner = type.substr(7, type.size() - 8); // remove "(Array " and ")"
        // Split into two sorts (handle nested parentheses)
        size_t depth = 0;
        size_t split = std::string::npos;
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '(') depth++;
            else if (inner[i] == ')') depth--;
            else if (inner[i] == ' ' && depth == 0) {
                split = i;
                break;
            }
        }
        if (split == std::string::npos) {
            throw std::runtime_error("Invalid Array sort: " + type);
        }
        std::string index_sort_str = inner.substr(0, split);
        std::string elem_sort_str = inner.substr(split + 1);
        cvc5::Sort index_sort = getCVC5Sort(index_sort_str);
        cvc5::Sort elem_sort = getCVC5Sort(elem_sort_str);
        return m_tm.mkArraySort(index_sort, elem_sort);
    } else {
        throw std::runtime_error("Type SMT-LIB2 non supporté: " + type);
    }
}
