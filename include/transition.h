#ifndef TRANSITION_H
#define TRANSITION_H

#include <vector>
#include <map>
#include <string>
#include <memory>

#include "linear_inequality.h"
#include "linearization/formula_linearizer.h"
#include "smtsolvers/SMTSolverInterface.h"

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
    
    bool isTrue() const;
    std::string getSSAVar(std::string prog_var, bool out_vars) const;

    // Export
    std::string toString() const;
    std::string toSMTLib2() const;
};



#endif
