#ifndef SMT_PARSER_H
#define SMT_PARSER_H

#include <string>
#include <vector>

#include "linear_inequality.h"
#include "parser/sexpr_utils.h"
#include "transition.h"

struct DNFFormula;

class SMTParser {
public:
    // Parse une formule SMT et extrait les inégalités linéaires
    // IMPORTANT: Les égalités (= a b) génèrent DEUX contraintes: a-b >= 0 ET b-a >= 0
    // Support: and, or, not, imbrications complexes
    static std::vector<LinearInequality> parseFormula(const std::string& smtFormula);
    
    // Parser récursif vers DNF (Disjunctive Normal Form)
    static DNFFormula parseFormulaToDNF(const std::string& smtFormula);
    
    // Parse une formule et extrait les variables avec leur version SSA
    static void extractVariables(const std::string& smtFormula,
                                std::vector<std::string>& vars);
private:
    
    // Parser formule atomique (=, <, >, <=, >=, store)
    static std::vector<LinearInequality> parseAtomicFormula(const std::string& formula);
    
    // Négation d'une conjonction - RETOURNE UNE DNF
    static DNFFormula negateConjunction(const std::vector<LinearInequality>& conjunction);
    
    // Distribuer AND sur plusieurs DNF
    static DNFFormula distributeAND(const std::vector<DNFFormula>& operands);
    
    // Helpers de parsing existants
    static LinearInequality parseInequality(const std::string& expr);
    static AffineTerm parseArithExpr(const std::string& expr);
    static bool isArithmeticOp(const std::string& op);
    static std::vector<std::string> splitSExpr(const std::string& expr) {
        return SExprUtils::splitSExpr(expr);
    }
};

#endif