#ifndef TRANSITION_H
#define TRANSITION_H

#include <vector>
#include <map>
#include <string>
#include <memory>

#include "linear_inequality.h"
#include "parser/smt_parser.h"
#include "smtsolvers/SMTSolverInterface.h"

// Structure pour DNF (Disjunctive Normal Form)
struct DNFFormula {
    std::vector<std::vector<LinearInequality>> polyhedra;

    std::string toString() const {
        std::string res = "\t\t";
        for (size_t i = 0; i < polyhedra.size(); ++i) {
            res += "(";
            for (size_t j = 0; j < polyhedra[i].size(); ++j) {
                res += polyhedra[i][j].toString();
                if (j < polyhedra[i].size() - 1) res += " AND ";
            }
            res += ")";
            if (i < polyhedra.size() - 1) res += "\n\t --- OR --- \n\t\t";
        }
        return res;
    }
};

/**
 * Structure pour représenter une transition avec ses métadonnées
 * Cette structure correspond exactement à une ligne du fichier counter.txt
 */
struct UltimateTransitionLine {
    // Identifiant de la transition (ex: "L111")
    std::string label;
    
    // Formule SMT brute (ex: "(= v___tmp__now_16 v_now_12)")
    std::string formula;
    
    // Mapping : variable_de_programme → version_SSA pour les entrées
    // Exemple : {"now" → "v_now_12", "count_Counter" → "v_count_Counter_19"}
    std::map<std::string, std::string> in_vars;
    
    // Mapping : variable_de_programme → version_SSA pour les sorties
    std::map<std::string, std::string> out_vars;
    
    // Les contraintes parsées depuis la formule SMT (structure DNF complète)
    DNFFormula dnf;
    
    // Variables booléennes si présentes
    std::map<std::string, bool> bool_vars;

    // Variables SSA libres dans la formule (ni in_vars ni out_vars).
    // Typiquement les variables auxiliaires Ultimate (div_aux, mod_aux, etc.)
    // du mode PREPROCESSED LINEAR TRACE.  Elles doivent être déclarées dans
    // le solver mais n'ont pas de coefficient dans la fonction de ranking.
    std::vector<std::string> free_vars;
};


// Transition linéaire : ensemble de polyèdres (disjonction de conjonctions)
class LinearTransition {
public:
    // Mapping : variable_de_programme → version_SSA pour les entrées
    // Exemple : {"now" → "v_now_12", "count_Counter" → "v_count_Counter_19"}
    std::map<std::string, std::string> var_to_ssa_in;
    // Mapping : variable_de_programme → version_SSA pour les sorties
    std::map<std::string, std::string> var_to_ssa_out;

    std::vector<std::vector<LinearInequality>> polyhedra;  // DNF

    LinearTransition();
    
    // Construction
    void addPolyhedron(const std::vector<LinearInequality>& poly);
    
    // Opérations de composition
    /**
     * MÉTHODE DE COMPOSITION (avec métadonnées)
     * Cette méthode utilise les mappings var_to_ssa_in et var_to_ssa_out
     * pour faire la substitution basée sur les variables de programme
     *
     * Algorithme :
     * 1. Pour chaque variable de programme commune entre this.var_to_ssa_out et other.var_to_ssa_in
     * 2. Créer une substitution : other.in_ssa → this.out_ssa
     * 3. Appliquer la substitution aux contraintes de other
     * 4. Combiner les contraintes de this et other (substitué)
     */
    LinearTransition compose(const LinearTransition& other) const;

    /**
     * Build a single LinearTransition by composing a sequence of transitions.
     *
     * For each consecutive pair Ti, Ti+1, the OutVars of Ti are matched
     * to the InVars of Ti+1 to build the substitution table.
     */
    static LinearTransition buildFromLines(
        const std::vector<UltimateTransitionLine>& lines
    );
    
    bool isTrue() const;
    std::string getSSAVar(std::string prog_var, bool out_vars) const;

    // Export
    std::string toString() const;
    std::string toSMTLib2() const;
};



#endif
