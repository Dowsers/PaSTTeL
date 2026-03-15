#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <cctype>

#include "smtsolvers/SMTSolverZ3.h"

// std::gcd ne supporte pas __int128 en C++17 — implémentation locale
static __int128 gcd128(__int128 a, __int128 b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { __int128 t = b; b = a % b; a = t; }
    return a;
}

// ============================================================================
// CONSTRUCTEUR / DESTRUCTEUR
// ============================================================================

SMTSolverZ3::SMTSolverZ3(bool verbose)
    : m_context()
    , m_solver(m_context)
    , m_assertion_count(0)
    , m_verbose(verbose)
    , m_interrupted(false)
{
    if (m_verbose) {
        std::cout << "[Z3] Initialisation du solveur Z3" << std::endl;
    }
    
    // Configuration du solveur pour optimiser les performances
    // z3::params params(m_context);
    // params.set("logic", "QF_LRA");
    // params.set("auto_config", false);
    // m_solver.set(params);
    // params.set("timeout", 300000u);  // 5 minutes timeout
    // m_solver.set(params);
}

// ============================================================================
// PUSH / POP
// ============================================================================

void SMTSolverZ3::push() {
    if (m_interrupted) return;
    try {
        m_solver.push();
    } catch (const z3::exception&) {
        m_interrupted = true;
        return;
    }
    m_assertion_stack.push(m_assertion_count);

    if (m_verbose) {
        std::cout << "[Z3] Push (niveau " << m_assertion_stack.size() << ")" << std::endl;
    }
}

void SMTSolverZ3::pop() {
    if (m_interrupted) return;
    if (m_assertion_stack.empty()) {
        throw std::runtime_error("SMTSolverZ3::pop() appelé sans push() correspondant");
    }
    
    try {
        m_solver.pop();
    } catch (const z3::exception&) {
        m_interrupted = true;
        return;
    }
    m_assertion_count = m_assertion_stack.top();
    m_assertion_stack.pop();
    
    if (m_verbose) {
        std::cout << "[Z3] Pop (niveau " << m_assertion_stack.size() << ")" << std::endl;
    }
}

// ============================================================================
// DÉCLARATION DE VARIABLES
// ============================================================================

void SMTSolverZ3::declareVariable(const std::string& name, const std::string& type) {
    if (m_interrupted) return;
    // Vérifier si la variable existe déjà
    if (variableExists(name)) {
        if (m_verbose) {
            std::cout << "[Z3] Variable '" << name << "' déjà déclarée" << std::endl;
        }
        return;
    }
    
    // Créer la variable avec le bon type
    z3::sort sort = getZ3Sort(type);
    z3::expr var = m_context.constant(name.c_str(), sort);
    
    // Ajouter au cache
    // m_variables[name] = var;
    m_variables.insert(std::make_pair(name, var));
    
    if (m_verbose) {
        std::cout << "[Z3] Variable déclarée : " << name << " : " << type << std::endl;
    }
}

// ============================================================================
// DÉCLARATION DE FONCTIONS NON INTERPRÉTÉES
// ============================================================================

void SMTSolverZ3::declareFunction(const std::string& name, const std::string& signature) {
    // Vérifier si la fonction existe déjà
    if (m_functions.find(name) != m_functions.end()) {
        if (m_verbose) {
            std::cout << "[Z3] Function '" << name << "' already declared" << std::endl;
        }
        return;
    }

    // Parser la signature: "(Type1 Type2 ...) ReturnType" ou "Type1 ReturnType"
    std::vector<z3::sort> domain_sorts;
    z3::sort range_sort = m_context.int_sort(); // default

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
            domain_sorts.push_back(getZ3Sort(type));
        }

        // Extraire le type de retour
        std::string range_str = sig.substr(close + 1);
        range_str.erase(0, range_str.find_first_not_of(" \t"));
        if (!range_str.empty()) {
            range_sort = getZ3Sort(range_str);
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
            domain_sorts.push_back(getZ3Sort(types[i]));
        }
        // Le dernier = codomaine
        range_sort = getZ3Sort(types.back());
    }

    // Créer la déclaration de fonction Z3
    z3::func_decl func = m_context.function(
        name.c_str(),
        static_cast<unsigned>(domain_sorts.size()),
        domain_sorts.data(),
        range_sort
    );

    // Ajouter au cache
    m_functions.insert(std::make_pair(name, func));

    if (m_verbose) {
        std::cout << "[Z3] Function declared: " << name << " : " << signature << std::endl;
    }
}

// ============================================================================
// AJOUT D'AXIOMES
// ============================================================================

void SMTSolverZ3::addAxiom(const std::string& axiom) {
    // Stocker l'axiome pour propagation lors du clonage
    m_axioms.push_back(axiom);

    try {
        // Parser l'axiome SMT-LIB2 et l'ajouter comme assertion
        z3::expr axiom_expr = parseSmtLib2(axiom);

        // Ajouter au solveur
        m_solver.add(axiom_expr);
        m_assertion_count++;

        if (m_verbose) {
            std::cout << "[Z3] Axiom added: " << axiom << std::endl;
        }

    } catch (const z3::exception& e) {
        std::cerr << "[Z3 ERROR] Error adding axiom: " << axiom << std::endl;
        std::cerr << "[Z3 ERROR] " << e.msg() << std::endl;
        throw;
    }
}

// ============================================================================
// AJOUT D'ASSERTIONS
// ============================================================================

void SMTSolverZ3::addAssertion(const std::string& assertion) {
    if (m_interrupted) return;
    try {
        // Parser l'assertion SMT-LIB2 et la convertir en expr Z3
        z3::expr constraint = parseSmtLib2(assertion);
        
        // Ajouter au solveur
        m_solver.add(constraint);
        m_assertion_count++;
        
        if (m_verbose) {
            std::cout << "[Z3] Assertion ajoutée (#" << m_assertion_count << "): "
                    << assertion << std::endl;
        }
        
    } catch (const z3::exception& e) {
        m_interrupted = true;
        if (m_verbose)
            std::cerr << "[Z3 ERROR] Erreur lors de l'ajout de l'assertion (interrupted): " << e.msg() << std::endl;
    }
}

// ============================================================================
// VÉRIFICATION SAT
// ============================================================================

bool SMTSolverZ3::checkSat() {
    if (m_verbose) {
        std::cout << "[Z3] Vérification de la satisfiabilité..." << std::endl;
    }
    
    z3::check_result result;
    try {
        result = m_solver.check();
    } catch (const z3::exception& e) {
        if (m_verbose)
            std::cout << "[Z3] CheckSat interrompu" << std::endl;
        return false;
    }

    bool is_sat = (result == z3::sat);

    if (m_verbose) {
        std::cout << "[Z3] Résultat: ";
        switch (result) {
            case z3::sat:
                std::cout << "SAT" << std::endl;
                break;
            case z3::unsat:
                std::cout << "UNSAT" << std::endl;
                break;
            case z3::unknown:
                std::cout << "UNKNOWN " << std::endl;
                std::cout << "[Z3] Raison: " << m_solver.reason_unknown() << std::endl;
                break;
        }
    }

    return is_sat;
}

// ============================================================================
// EXTRACTION DE VALEURS
// ============================================================================

double SMTSolverZ3::getValue(const std::string& var_name) {
    // Vérifier que la variable existe
    if (!variableExists(var_name)) {
        throw std::runtime_error("SMTSolverZ3::getValue() : variable '" + var_name + "' non déclarée");
    }
    
    // Obtenir le modèle
    z3::model model = m_solver.get_model();
    
    // Évaluer la variable
    z3::expr var = getVariable(var_name);
    z3::expr value = model.eval(var, true);
    
    // Convertir en double
    double result = 0.0;
    
    if (value.is_bool()) {
        // Cas booléen (testé en premier)
        result = value.is_true() ? 1.0 : 0.0;
    } else if (value.is_numeral()) {
        // Cas numérique
        if (value.is_int()) {
            // Entier
            result = static_cast<double>(value.get_numeral_int64());
        } else if (value.is_real()) {
            // Réel : d'abord tenter l'extraction exacte int64 numérateur/dénominateur,
            // puis fallback vers get_numeral_double si les nombres débordent int64.
            bool extracted = false;
            try {
                int64_t num = value.numerator().get_numeral_int64();
                int64_t den = value.denominator().get_numeral_int64();
                result = static_cast<double>(num) / static_cast<double>(den);
                extracted = true;
            } catch (...) {}

            if (!extracted) {
                // Fallback : Z3 is_numeral(double&) gère nativement les grands rationnels.
                double d = 0.0;
                if (value.is_numeral(d)) {
                    result = d;
                } else {
                    throw std::runtime_error("SMTSolverZ3::getValue() : impossible de parser le réel " + value.to_string());
                }
            }
        } else {
            throw std::runtime_error("SMTSolverZ3::getValue() : type numérique inconnu pour " + var_name);
        }
    } else {
        throw std::runtime_error("SMTSolverZ3::getValue() : type non supporté pour " + var_name);
    }
    
    if (m_verbose) {
        std::cout << "[Z3] " << var_name << " = " << result << std::endl;
    }
    
    return result;
}

std::pair<int64_t, int64_t> SMTSolverZ3::getRationalValue(const std::string& var_name) {
    if (!variableExists(var_name)) {
        throw std::runtime_error("SMTSolverZ3::getRationalValue() : variable '" + var_name + "' non declaree");
    }
    z3::model model = m_solver.get_model();
    z3::expr var = getVariable(var_name);
    z3::expr value = model.eval(var, true);

    if (value.is_int() && value.is_numeral()) {
        try {
            return { value.get_numeral_int64(), 1 };
        } catch (...) {
            // Overflow: parse via __int128 and clamp to int64
            using i128 = __int128;
            std::string s = value.to_string();
            bool neg = (!s.empty() && s[0] == '-');
            i128 v = 0;
            for (char c : s) {
                if (c == '-' || c == '+') continue;
                if (c < '0' || c > '9') break;
                v = v * 10 + (c - '0');
            }
            if (neg) v = -v;
            constexpr i128 MAX64 = (i128)INT64_MAX;
            constexpr i128 MIN64 = (i128)INT64_MIN;
            if (v > MAX64) v = MAX64;
            if (v < MIN64) v = MIN64;
            return { (int64_t)v, 1 };
        }
    } else if (value.is_real() && value.is_numeral()) {
        try {
            int64_t num = value.numerator().get_numeral_int64();
            int64_t den = value.denominator().get_numeral_int64();
            if (den < 0) { num = -num; den = -den; }
            return { num, den };
        } catch (...) {
            // get_numeral_int64 overflowed: use string parsing.
            // Z3 may produce "(/ NUM.0 DEN.0)" or "NUM/DEN" or "NUM.0".
            std::string s = value.to_string();

            // Strip surrounding parens and leading "/ " if present
            // e.g. "(/ 32281802098926944255.0 8589934598.0)" -> "32281802098926944255.0 / 8589934598.0"
            // or bare "32281802098926944255/8589934598"
            // Normalize: remove '(' ')' then look for the slash
            std::string cleaned;
            cleaned.reserve(s.size());
            for (char c : s) {
                if (c != '(' && c != ')') cleaned += c;
            }
            // cleaned is now "/ 32281802098926944255.0 8589934598.0" or "32281802098926944255/8589934598"

            // Find slash (skip leading '/ ' if present)
            size_t slash = cleaned.find('/');
            if (slash != std::string::npos) {
                std::string num_str = cleaned.substr(0, slash);
                std::string den_str = cleaned.substr(slash + 1);

                // Strip whitespace
                auto strip = [](std::string& t) {
                    size_t a = t.find_first_not_of(" \t");
                    size_t b = t.find_last_not_of(" \t");
                    if (a == std::string::npos) { t.clear(); return; }
                    t = t.substr(a, b - a + 1);
                };
                strip(num_str);
                strip(den_str);

                // Strip ".0" decimal suffix if present (Z3 sometimes appends it)
                auto strip_dot_zero = [](std::string& t) {
                    if (t.size() >= 2 && t.back() == '0' && t[t.size()-2] == '.') {
                        t.resize(t.size() - 2);
                    }
                };
                strip_dot_zero(num_str);
                strip_dot_zero(den_str);

                // If num_str is empty (was "/ NUM DEN" form — slash was at pos 0)
                // then actually the number part is den_str and there's no denominator
                if (num_str.empty()) {
                    // Fallback to double
                    try {
                        double d = std::stod(den_str);
                        return { static_cast<int64_t>(std::round(d)), 1 };
                    } catch (...) {}
                }

                // Try parsing as int64.
                // If either number overflows int64, use __int128 to compute
                // the reduced fraction (num/gcd, den/gcd) and clamp to int64.
                (void)(!num_str.empty() && num_str[0] == '-');  // neg_num unused
                (void)(!den_str.empty() && den_str[0] == '-');  // neg_den unused
                try {
                    int64_t num = std::stoll(num_str);
                    int64_t den = std::stoll(den_str);
                    if (den < 0) { num = -num; den = -den; }
                    return { num, den };
                } catch (...) {
                    // Numbers too large for int64: parse into __int128 and reduce.
                    using i128 = __int128;
                    auto parse_i128 = [](const std::string& s) -> i128 {
                        bool neg = (!s.empty() && s[0] == '-');
                        i128 v = 0;
                        for (char c : s) {
                            if (c == '-' || c == '+') continue;
                            if (c < '0' || c > '9') break;
                            v = v * 10 + (c - '0');
                        }
                        return neg ? -v : v;
                    };
                    i128 bn = parse_i128(num_str);
                    i128 bd = parse_i128(den_str);
                    if (bd < 0) { bn = -bn; bd = -bd; }
                    if (bd == 0) bd = 1;
                    i128 g = gcd128(bn, bd);
                    if (g > 1) { bn /= g; bd /= g; }
                    // Clamp to int64 (saturation)
                    constexpr i128 MAX64 = (i128)INT64_MAX;
                    constexpr i128 MIN64 = (i128)INT64_MIN;
                    if (bn > MAX64) bn = MAX64;
                    if (bn < MIN64) bn = MIN64;
                    if (bd > MAX64) bd = MAX64;
                    return { (int64_t)bn, (int64_t)bd };
                }
            }

            // No slash: try as a simple number
            // Strip ".0" suffix
            if (s.size() >= 2 && s.back() == '0' && s[s.size()-2] == '.') {
                s.resize(s.size() - 2);
            }
            try {
                return { std::stoll(s), 1 };
            } catch (...) {
                return { static_cast<int64_t>(std::round(std::stod(s))), 1 };
            }
        }
    }
    // Fallback
    double v = getValue(var_name);
    int64_t den = 1;
    while (std::abs(v * den - std::round(v * den)) > 1e-9 && den < (1LL << 20)) den *= 2;
    return { static_cast<int64_t>(std::round(v * den)), den };
}

// ============================================================================
// COMPTEUR D'ASSERTIONS
// ============================================================================

size_t SMTSolverZ3::getAssertionCount() const {
    return m_assertion_count;
}

// ============================================================================
// MÉTHODES UTILITAIRES
// ============================================================================

z3::expr SMTSolverZ3::getVariable(const std::string& name) {
    auto it = m_variables.find(name);
    if (it == m_variables.end()) {
        throw std::runtime_error("Variable '" + name + "' non déclarée");
    }
    return it->second;
}

void SMTSolverZ3::interrupt() {
    m_interrupted = true;
    m_context.interrupt();
    if (m_verbose) {
        std::cout << "[Z3] CheckSat interrompu" << std::endl;
    }
}

void SMTSolverZ3::printStatistics() const {
    std::cout << "\n╔══════════════════════════════════════╗" << std::endl;
    std::cout << "║  Z3 Solveur - Statistiques          ║" << std::endl;
    std::cout << "╚══════════════════════════════════════╝" << std::endl;
    std::cout << "  Variables déclarées : " << m_variables.size() << std::endl;
    std::cout << "  Assertions ajoutées : " << m_assertion_count << std::endl;
    std::cout << "  Niveau de push/pop  : " << m_assertion_stack.size() << std::endl;
    
    // Statistiques Z3
    // z3::stats stats = m_solver.statistics();
    // std::cout << "\n  Statistiques Z3:" << std::endl;
    // for (unsigned i = 0; i < stats.size(); i++) {
    //     std::cout << "    " << stats.key(i) << " : " << stats.double_value(i) << std::endl;
    // }
}

void SMTSolverZ3::printModel() const {
    try {
        z3::model model = m_solver.get_model();
        std::cout << "\n╔══════════════════════════════════════╗" << std::endl;
        std::cout << "║  Z3 Modèle                           ║" << std::endl;
        std::cout << "╚══════════════════════════════════════╝" << std::endl;
        
        for (unsigned i = 0; i < model.size(); i++) {
            z3::func_decl decl = model[i];
            std::cout << "  " << decl.name() << " = " << model.get_const_interp(decl) << std::endl;
        }
    } catch (const z3::exception& e) {
        std::cerr << "[Z3 ERROR] Impossible d'afficher le modèle: " << e.msg() << std::endl;
    }
}

void SMTSolverZ3::reset() {
    m_solver.reset();
    m_variables.clear();
    m_functions.clear();  // Clear function declarations too
    m_axioms.clear();     // Clear axioms too
    m_assertion_count = 0;
    while (!m_assertion_stack.empty()) {
        m_assertion_stack.pop();
    }

    if (m_verbose) {
        std::cout << "[Z3] Solveur réinitialisé" << std::endl;
    }
}

bool SMTSolverZ3::variableExists(const std::string& name) const {
    return m_variables.find(name) != m_variables.end();
}

// ============================================================================
// CLONAGE
// ============================================================================

std::shared_ptr<SMTSolver> SMTSolverZ3::clone() const {
    // Créer une nouvelle instance avec les mêmes paramètres
    auto cloned = std::make_shared<SMTSolverZ3>(m_verbose);

    // Copier les variables déclarées
    for (const auto& [name, expr] : m_variables) {
        std::string sort_name;
        z3::sort s = expr.get_sort();
        if (s.is_int()) {
            sort_name = "Int";
        } else if (s.is_real()) {
            sort_name = "Real";
        } else if (s.is_bool()) {
            sort_name = "Bool";
        } else if (s.is_array()) {
            // Reconstruire "(Array <index_sort> <element_sort>)"
            z3::sort idx = s.array_domain();
            z3::sort elem = s.array_range();
            std::string idx_str = idx.is_int() ? "Int" : idx.is_real() ? "Real" : idx.name().str();
            std::string elem_str = elem.is_int() ? "Int" : elem.is_real() ? "Real" : elem.name().str();
            sort_name = "(Array " + idx_str + " " + elem_str + ")";
        } else {
            sort_name = s.name().str();
        }
        cloned->declareVariable(name, sort_name);
    }

    // Copier les fonctions déclarées
    for (const auto& [name, func] : m_functions) {
        // Reconstruire la signature depuis func_decl
        std::ostringstream sig;

        unsigned arity = func.arity();
        if (arity > 0) {
            sig << "(";
            for (unsigned i = 0; i < arity; ++i) {
                if (i > 0) sig << " ";
                sig << func.domain(i).name();
            }
            sig << ") ";
        }
        sig << func.range().name();

        cloned->declareFunction(name, sig.str());
    }

    // Copier les axiomes
    for (const auto& axiom : m_axioms) {
        cloned->addAxiom(axiom);
    }

    return cloned;
}

// ============================================================================
// PARSING SMT-LIB2 (direct Z3 API — no string re-parsing)
// ============================================================================

// Tokenize an S-expression into a flat list of tokens
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
        } else if (std::isspace(static_cast<unsigned char>(c))) {
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

z3::expr SMTSolverZ3::parseSmtLib2(const std::string& smt_string) {
    std::string trimmed = smt_string;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

    std::vector<std::string> tokens = tokenize_sexp(trimmed);
    size_t pos = 0;
    return parseSexpTokens(tokens, pos);
}

z3::expr SMTSolverZ3::parseSexpTokens(
    const std::vector<std::string>& tokens, size_t& pos) {
    return parseSexpTokensWithBindings(tokens, pos, {});
}

z3::expr SMTSolverZ3::parseSexpTokensWithBindings(
    const std::vector<std::string>& tokens,
    size_t& pos,
    const std::map<std::string, z3::expr>& bound_vars) {

    if (pos >= tokens.size()) {
        throw std::runtime_error("Unexpected end of expression");
    }

    const std::string& token = tokens[pos];

    // Case 1: S-expression (op arg1 arg2 ...)
    if (token == "(") {
        pos++; // consume '('

        if (pos >= tokens.size()) {
            throw std::runtime_error("Unexpected end after '('");
        }

        std::string op = tokens[pos++]; // read operator

        // Special case: quantifiers forall / exists
        if (op == "forall" || op == "exists") {
            return parseQuantifier(op, tokens, pos, bound_vars);
        }

        // Special case: let bindings
        if (op == "let") {
            return parseLet(tokens, pos, bound_vars);
        }

        // Collect arguments
        std::vector<z3::expr> args;
        while (pos < tokens.size() && tokens[pos] != ")") {
            args.push_back(parseSexpTokensWithBindings(tokens, pos, bound_vars));
        }

        if (pos >= tokens.size() || tokens[pos] != ")") {
            throw std::runtime_error("Missing closing parenthesis");
        }
        pos++; // consume ')'

        return buildTerm(op, args);
    }

    // Case 2: Atom (bound variable, global variable, number, or constant)
    pos++;

    // Check bound variables first (priority over globals)
    {
        auto it = bound_vars.find(token);
        if (it != bound_vars.end()) {
            return it->second;
        }
    }

    // Global variable
    if (variableExists(token)) {
        return getVariable(token);
    }

    // Boolean constants
    if (token == "true")  return m_context.bool_val(true);
    if (token == "false") return m_context.bool_val(false);

    // Number (integer or real)
    if (!token.empty() &&
        (std::isdigit(static_cast<unsigned char>(token[0])) ||
         (token[0] == '-' && token.length() > 1))) {
        if (token.find('.') != std::string::npos) {
            return m_context.real_val(token.c_str());
        } else {
            return m_context.int_val(token.c_str());
        }
    }

    throw std::runtime_error("Unknown atom in SMT-LIB2 expression: " + token);
}

z3::expr SMTSolverZ3::parseQuantifier(
    const std::string& quantifier,
    const std::vector<std::string>& tokens,
    size_t& pos,
    const std::map<std::string, z3::expr>& outer_bound_vars) {

    // Structure: (forall ((var1 Type1) (var2 Type2) ...) body)
    if (pos >= tokens.size() || tokens[pos] != "(") {
        throw std::runtime_error(quantifier + " expects binding list");
    }
    pos++; // consume '(' of binding list

    std::vector<z3::expr> bound_var_exprs;
    std::map<std::string, z3::expr> new_bound_vars = outer_bound_vars;

    while (pos < tokens.size() && tokens[pos] == "(") {
        pos++; // consume '(' of individual binding

        if (pos + 1 >= tokens.size()) {
            throw std::runtime_error("Incomplete variable binding");
        }

        std::string var_name = tokens[pos++];
        std::string var_type = tokens[pos++];

        if (pos >= tokens.size() || tokens[pos] != ")") {
            throw std::runtime_error("Missing ) in variable binding");
        }
        pos++; // consume ')'

        z3::sort sort = getZ3Sort(var_type);
        z3::expr bound_var = m_context.constant(var_name.c_str(), sort);
        bound_var_exprs.push_back(bound_var);
        new_bound_vars.insert_or_assign(var_name, bound_var);
    }

    // consume ')' of binding list
    if (pos >= tokens.size() || tokens[pos] != ")") {
        throw std::runtime_error("Missing ) after binding list");
    }
    pos++;

    // Parse body with new bindings
    z3::expr body = parseSexpTokensWithBindings(tokens, pos, new_bound_vars);

    // consume ')' of quantifier
    if (pos >= tokens.size() || tokens[pos] != ")") {
        throw std::runtime_error("Missing ) after " + quantifier + " body");
    }
    pos++;

    // Build quantified expression
    z3::expr_vector bound_vec(m_context);
    for (auto& bv : bound_var_exprs) {
        bound_vec.push_back(bv);
    }

    if (quantifier == "forall") {
        return z3::forall(bound_vec, body);
    } else {
        return z3::exists(bound_vec, body);
    }
}

z3::expr SMTSolverZ3::parseLet(
    const std::vector<std::string>& tokens,
    size_t& pos,
    const std::map<std::string, z3::expr>& outer_bound_vars) {

    // Structure: (let ((var1 expr1) (var2 expr2) ...) body)
    if (pos >= tokens.size() || tokens[pos] != "(") {
        throw std::runtime_error("let expects binding list");
    }
    pos++; // consume '(' of binding list

    std::map<std::string, z3::expr> new_bound_vars = outer_bound_vars;

    while (pos < tokens.size() && tokens[pos] == "(") {
        pos++; // consume '(' of individual binding

        if (pos >= tokens.size()) {
            throw std::runtime_error("Incomplete let binding");
        }

        std::string var_name = tokens[pos++];
        z3::expr var_expr = parseSexpTokensWithBindings(tokens, pos, outer_bound_vars);

        if (pos >= tokens.size() || tokens[pos] != ")") {
            throw std::runtime_error("Missing ) in let binding");
        }
        pos++; // consume ')'

        new_bound_vars.insert_or_assign(var_name, var_expr);
    }

    // consume ')' of binding list
    if (pos >= tokens.size() || tokens[pos] != ")") {
        throw std::runtime_error("Missing ) after let binding list");
    }
    pos++;

    // Parse body with new bindings
    z3::expr body = parseSexpTokensWithBindings(tokens, pos, new_bound_vars);

    // consume ')' of let
    if (pos >= tokens.size() || tokens[pos] != ")") {
        throw std::runtime_error("Missing ) after let body");
    }
    pos++;

    return body;
}

z3::expr SMTSolverZ3::buildTerm(
    const std::string& op, const std::vector<z3::expr>& args) {

    // Arithmetic operators
    if (op == "+") {
        if (args.empty()) throw std::runtime_error("+ requires at least 1 argument");
        if (args.size() == 1) return args[0];
        z3::expr result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = result + args[i];
        }
        return result;
    }

    if (op == "-") {
        if (args.empty()) throw std::runtime_error("- requires at least 1 argument");
        if (args.size() == 1) return -args[0];
        z3::expr result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = result - args[i];
        }
        return result;
    }

    if (op == "*") {
        if (args.empty()) throw std::runtime_error("* requires at least 1 argument");
        if (args.size() == 1) return args[0];
        z3::expr result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = result * args[i];
        }
        return result;
    }

    if (op == "/") {
        if (args.size() != 2) throw std::runtime_error("/ requires exactly 2 arguments");
        return args[0] / args[1];
    }

    if (op == "div") {
        if (args.size() != 2) throw std::runtime_error("div requires exactly 2 arguments");
        return z3::expr(m_context, Z3_mk_div(m_context, args[0], args[1]));
    }

    if (op == "mod") {
        if (args.size() != 2) throw std::runtime_error("mod requires exactly 2 arguments");
        return z3::mod(args[0], args[1]);
    }

    // Comparison operators
    if (op == "=") {
        if (args.size() != 2) throw std::runtime_error("= requires exactly 2 arguments");
        return args[0] == args[1];
    }

    if (op == "<") {
        if (args.size() != 2) throw std::runtime_error("< requires exactly 2 arguments");
        return args[0] < args[1];
    }

    if (op == "<=") {
        if (args.size() != 2) throw std::runtime_error("<= requires exactly 2 arguments");
        return args[0] <= args[1];
    }

    if (op == ">") {
        if (args.size() != 2) throw std::runtime_error("> requires exactly 2 arguments");
        return args[0] > args[1];
    }

    if (op == ">=") {
        if (args.size() != 2) throw std::runtime_error(">= requires exactly 2 arguments");
        return args[0] >= args[1];
    }

    // Logical operators
    if (op == "and") {
        if (args.empty()) throw std::runtime_error("and requires at least 1 argument");
        if (args.size() == 1) return args[0];
        z3::expr result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = result && args[i];
        }
        return result;
    }

    if (op == "or") {
        if (args.empty()) throw std::runtime_error("or requires at least 1 argument");
        if (args.size() == 1) return args[0];
        z3::expr result = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            result = result || args[i];
        }
        return result;
    }

    if (op == "not") {
        if (args.size() != 1) throw std::runtime_error("not requires exactly 1 argument");
        return !args[0];
    }

    if (op == "=>") {
        if (args.size() != 2) throw std::runtime_error("=> requires exactly 2 arguments");
        return z3::implies(args[0], args[1]);
    }

    if (op == "ite") {
        if (args.size() != 3) throw std::runtime_error("ite requires exactly 3 arguments");
        return z3::ite(args[0], args[1], args[2]);
    }

    if (op == "distinct") {
        if (args.size() < 2) throw std::runtime_error("distinct requires at least 2 arguments");
        z3::expr_vector vec(m_context);
        for (const auto& a : args) vec.push_back(a);
        return z3::distinct(vec);
    }

    // Array operators
    if (op == "select") {
        if (args.size() != 2) throw std::runtime_error("select requires exactly 2 arguments");
        return z3::select(args[0], args[1]);
    }

    if (op == "store") {
        if (args.size() != 3) throw std::runtime_error("store requires exactly 3 arguments");
        return z3::store(args[0], args[1], args[2]);
    }

    // Uninterpreted function application
    {
        auto it = m_functions.find(op);
        if (it != m_functions.end()) {
            z3::expr_vector arg_vec(m_context);
            for (const auto& a : args) arg_vec.push_back(a);
            return it->second(arg_vec);
        }
    }

    throw std::runtime_error("Unknown operator in SMT-LIB2 expression: " + op);
}

// Keep parseSmtLib2Manual as a no-op fallback (unused but declared in header)
z3::expr SMTSolverZ3::parseSmtLib2Manual(const std::string& smt_string) {
    return parseSmtLib2(smt_string);
}

// ============================================================================
// CONVERSION DE TYPES
// ============================================================================

z3::sort SMTSolverZ3::getZ3Sort(const std::string& type) {
    if (type == "Int") {
        return m_context.int_sort();
    } else if (type == "Real") {
        return m_context.real_sort();
    } else if (type == "Bool") {
        return m_context.bool_sort();
    } else if (type.substr(0, 6) == "(Array") {
        // Parse "(Array <index_sort> <element_sort>)"
        // Find the two sub-sorts after "(Array "
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
        z3::sort index_sort = getZ3Sort(index_sort_str);
        z3::sort elem_sort = getZ3Sort(elem_sort_str);
        return m_context.array_sort(index_sort, elem_sort);
    } else {
        throw std::runtime_error("Type SMT-LIB2 non supporté: " + type);
    }
}