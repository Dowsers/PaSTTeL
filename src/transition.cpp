#include <iostream>

#include "transition.h"
#include "utiles.h"


// Constructeur par défaut - crée une transition avec un polyèdre vide
LinearTransition::LinearTransition() {
    // Initialise avec un polyèdre vide (pas de contraintes = true)
    polyhedra.push_back(std::vector<LinearInequality>());
}

// Ajoute un polyèdre complet à la disjonction
void LinearTransition::addPolyhedron(const std::vector<LinearInequality>& poly) {
    // Si c'est le premier ajout et qu'on avait juste le polyèdre vide par défaut
    if (polyhedra.size() == 1 && polyhedra[0].empty() && !poly.empty()) {
        polyhedra[0] = poly;  // Remplace le polyèdre vide
    } else {
        polyhedra.push_back(poly);  // Ajoute à la disjonction
    }
}

// Vérifie si la transition est "true" (aucune contrainte)
bool LinearTransition::isTrue() const {
    // "true" signifie : un seul polyèdre ET ce polyèdre est vide
    // Polyèdre vide = conjonction vide = true
    // Car : AND sur ensemble vide = élément neutre = true
    return polyhedra.size() == 1 && polyhedra[0].empty();
}

std::string LinearTransition::getSSAVar(std::string prog_var, bool out_vars) const {
    if(out_vars) {
        auto it = var_to_ssa_out.find(prog_var);
        if (it == var_to_ssa_out.end()) {
            throw std::runtime_error("getSSAVar: program variable '" + prog_var
                + "' not found in out_vars mapping");
        }
        return it->second;
    }
    auto it = var_to_ssa_in.find(prog_var);
    if (it == var_to_ssa_in.end()) {
        throw std::runtime_error("getSSAVar: program variable '" + prog_var
            + "' not found in in_vars mapping");
    }
    return it->second;
}

// ============================================================================
// FONCTION PRINCIPALE : Composition avec métadonnées InVars/OutVars
// ============================================================================
LinearTransition LinearTransition::compose(const LinearTransition& other) const {
    
if(VERBOSITY == VerbosityLevel::VERBOSE){
    std::cout << "\n┌─────────────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ COMPOSITION AVEC MÉTADONNÉES                                 │" << std::endl;
    std::cout << "└─────────────────────────────────────────────────────────────┘" << std::endl;
    
    // ═══════════════════════════════════════════════════════════════════════
    // ÉTAPE 1 : CONSTRUIRE LA TABLE DE SUBSTITUTION
    // ═══════════════════════════════════════════════════════════════════════
    
    std::cout << "\n[Étape 1] Construction de la table de substitution" << std::endl;
    std::cout << "────────────────────────────────────────────────────" << std::endl;
}

    // Pour chaque variable de programme, on va remplacer sa version SSA d'entrée dans T2
    // par sa version SSA de sortie dans T1
    std::map<std::string, std::string> substitution_map;
    
if(VERBOSITY == VerbosityLevel::VERBOSE){
    std::cout << "\nVariables de sortie de T1 (this) :" << std::endl;
    for (const auto& [var_prog, ssa_out_t1] : this->var_to_ssa_out) {
        std::cout << "  " << var_prog << " = " << ssa_out_t1 << std::endl;
    }
    
    std::cout << "\nVariables d'entrée de T2 (other) :" << std::endl;
    for (const auto& [var_prog, ssa_in_t2] : other.var_to_ssa_in) {
        std::cout << "  " << var_prog << " = " << ssa_in_t2 << std::endl;
    }

    std::cout << "\nSubstitutions à effectuer :" << std::endl;
}
    for (const auto& [var_prog, ssa_out_t1] : this->var_to_ssa_out) {
        // Vérifier si cette variable de programme existe aussi dans T2
        auto it = other.var_to_ssa_in.find(var_prog);
        if (it != other.var_to_ssa_in.end()) {
            std::string ssa_in_t2 = it->second;
            
            // Créer la substitution : ssa_in_t2 → ssa_out_t1
            substitution_map[ssa_in_t2] = ssa_out_t1;

if(VERBOSITY == VerbosityLevel::VERBOSE){
            std::cout << "  Variable '" << var_prog << "' : "
                    << ssa_in_t2 << " ⟶ " << ssa_out_t1 << std::endl;
}
            }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ÉTAPE 2 : CONSTRUIRE LA TRANSITION COMPOSÉE
    // ═══════════════════════════════════════════════════════════════════════
if(VERBOSITY == VerbosityLevel::VERBOSE){
    std::cout << "\n[Étape 2] Construction de la transition composée" << std::endl;
    std::cout << "────────────────────────────────────────────────────" << std::endl;
}
    LinearTransition result;
    
    // Métadonnées de la transition composée :
    // - Entrées : celles de T1
    // - Sorties : celles de T2 (après substitution)
    result.var_to_ssa_in = this->var_to_ssa_in;
    result.var_to_ssa_out = other.var_to_ssa_out;
    
    // Appliquer les substitutions aux métadonnées de sortie
    for (auto& [var_prog, ssa] : result.var_to_ssa_out) {
        auto sub_it = substitution_map.find(ssa);
        if (sub_it != substitution_map.end()) {
if(VERBOSITY == VerbosityLevel::VERBOSE){
            std::cout << "  Mise à jour OutVar : " << var_prog << " : "
                    << ssa << " → " << sub_it->second << std::endl;
}
            ssa = sub_it->second;

        }
    }
    
    result.polyhedra.clear();
    
    // ═══════════════════════════════════════════════════════════════════════
    // ÉTAPE 3 : COMPOSER LES POLYÈDRES
    // ═══════════════════════════════════════════════════════════════════════

if(VERBOSITY == VerbosityLevel::VERBOSE){
    std::cout << "\n[Étape 3] Composition des polyèdres" << std::endl;
    std::cout << "────────────────────────────────────────────────────" << std::endl;
}
    int poly_count = 0;
    for (const auto& poly1 : this->polyhedra) {
        for (const auto& poly2 : other.polyhedra) {
            poly_count++;
            
            std::vector<LinearInequality> composed_poly;
            
            // Ajouter les contraintes de T1 telles quelles
            for (const auto& ineq1 : poly1) {
                composed_poly.push_back(ineq1);
            }
            
            // Ajouter les contraintes de T2 AVEC SUBSTITUTION
            for (const auto& ineq2 : poly2) {
                LinearInequality substituted_ineq = ineq2;  // Copie
                
                // Pour chaque coefficient de la contrainte
                std::map<std::string, AffineTerm> new_coefficients;
                
                for (const auto& [var_ssa, coef] : ineq2.coefficients) {
                    std::string new_var_ssa = var_ssa;
                    
                    // Vérifier si c'est une variable indexée (array[index])
                    size_t bracket_pos = var_ssa.find('[');
                    if (bracket_pos != std::string::npos) {
                        // Extraire array et index
                        std::string array_part = var_ssa.substr(0, bracket_pos);
                        size_t close_bracket = var_ssa.find(']', bracket_pos);
                        std::string index_part = var_ssa.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
                        
                        bool array_substituted = false;
                        bool index_substituted = false;
                        
                        // Substituer l'array si nécessaire
                        auto array_sub = substitution_map.find(array_part);
                        if (array_sub != substitution_map.end()) {
                            array_part = array_sub->second;
                            array_substituted = true;
                        }
                        
                        // Substituer l'index si nécessaire
                        auto index_sub = substitution_map.find(index_part);
                        if (index_sub != substitution_map.end()) {
                            index_part = index_sub->second;
                            index_substituted = true;
                        }
                        
                        // Reconstruire le nom de variable
                        if (array_substituted || index_substituted) {
                            new_var_ssa = array_part + "[" + index_part + "]";
                        }
                    } else {
                        // Variable simple, substitution normale
                        auto sub_it = substitution_map.find(var_ssa);
                        if (sub_it != substitution_map.end()) {
                            new_var_ssa = sub_it->second;
                        }
                    }
                    
                    new_coefficients[new_var_ssa] = coef;
                }
                
                // Mettre à jour les coefficients de la contrainte
                substituted_ineq.coefficients = new_coefficients;
                
                // CRITIQUE: Substituer aussi dans constant.coefficients
                std::map<std::string, double> new_constant_coeffs;
                for (const auto& [var_ssa, coef_val] : ineq2.constant.coefficients) {
                    std::string new_var_ssa = var_ssa;
                    
                    // Array indexé
                    size_t bracket_pos = var_ssa.find('[');
                    if (bracket_pos != std::string::npos) {
                        std::string array_part = var_ssa.substr(0, bracket_pos);
                        size_t close_bracket = var_ssa.find(']', bracket_pos);
                        std::string index_part = var_ssa.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
                        
                        auto array_sub = substitution_map.find(array_part);
                        if (array_sub != substitution_map.end()) {
                            array_part = array_sub->second;
                        }
                        
                        auto index_sub = substitution_map.find(index_part);
                        if (index_sub != substitution_map.end()) {
                            index_part = index_sub->second;
                        }
                        
                        new_var_ssa = array_part + "[" + index_part + "]";
                    } else {
                        // Variable simple
                        auto sub_it = substitution_map.find(var_ssa);
                        if (sub_it != substitution_map.end()) {
                            new_var_ssa = sub_it->second;
                        }
                    }
                    
                    new_constant_coeffs[new_var_ssa] = coef_val;
                }
                substituted_ineq.constant.coefficients = new_constant_coeffs;
                
                composed_poly.push_back(substituted_ineq);
            }
            
            if (!composed_poly.empty()) {
                result.polyhedra.push_back(composed_poly);
if(VERBOSITY == VerbosityLevel::VERBOSE){
                std::cout << "  ✓ Polyèdre composé créé avec " << composed_poly.size()
                        << " contraintes" << std::endl;
}
            }
        }
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // ÉTAPE 4 : CAS SPÉCIAUX
    // ═══════════════════════════════════════════════════════════════════════
    
    // Si une des transitions est "true", simplifier
    if (this->isTrue()) {
        return other;
    }
    if (other.isTrue()) {
        return *this;
    }
    
    // Si pas de polyèdres résultants, la composition est impossible
    if (result.polyhedra.empty()) {
        std::cout << "\n[Attention] Aucun polyèdre résultant - transition impossible" << std::endl;
        LinearInequality false_ineq;
        false_ineq.constant = AffineTerm(-1.0);  // -1 >= 0 est toujours faux
        result.polyhedra.push_back({false_ineq});
    }

if(VERBOSITY == VerbosityLevel::VERBOSE){
    std::cout << "\n┌─────────────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ COMPOSITION TERMINÉE                                         │" << std::endl;
    std::cout << "└─────────────────────────────────────────────────────────────┘\n" << std::endl;
}
    return result;
}



// ============================================================================
// CONVERSION EN STRING
// ============================================================================

// Conversion en string lisible
std::string LinearTransition::toString() const {
    if (isTrue()) return "true";
    
    // Si tous les polyèdres sont vides (ne devrait pas arriver après isTrue)
    if (polyhedra.empty()) return "false";
    
    std::ostringstream oss;
    
    // Affiche chaque polyèdre
    for (size_t i = 0; i < polyhedra.size(); ++i) {
        if (i > 0) oss << "\n    ∨ ";  // Disjonction (OR)
        
        if (polyhedra[i].empty()) {
            oss << "true";
        } else {
            oss << "(";
            for (size_t j = 0; j < polyhedra[i].size(); ++j) {
                if (j > 0) oss << " ∧ ";  // Conjonction (AND)
                oss << polyhedra[i][j].toString();
            }
            oss << ")";
        }
    }
    
    return oss.str();
}

// Conversion en format SMT-LIB2
std::string LinearTransition::toSMTLib2() const {
    if (isTrue()) return "true";
    if (polyhedra.empty()) return "false";
    
    std::vector<std::string> disjuncts;
    
    for (const auto& poly : polyhedra) {
        if (poly.empty()) {
            // Polyèdre vide = true
            disjuncts.push_back("true");
        } else if (poly.size() == 1) {
            // Un seul constraint
            disjuncts.push_back(poly[0].toSMTLib2());
        } else {
            // Plusieurs constraints : conjonction
            std::string conj = "(and";
            for (const auto& ineq : poly) {
                conj += " " + ineq.toSMTLib2();
            }
            conj += ")";
            disjuncts.push_back(conj);
        }
    }
    
    // Construit la disjonction finale
    if (disjuncts.size() == 1) {
        return disjuncts[0];
    } else {
        std::string result = "(or";
        for (const auto& d : disjuncts) {
            result += " " + d;
        }
        result += ")";
        return result;
    }
}

LinearTransition LinearTransition::buildFromLines(
    const std::vector<UltimateTransitionLine>& lines
) {
    if (lines.empty()) {
        return LinearTransition();
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n--- Building transition from " << lines.size()
                << " JSON transitions ---" << std::endl;
    }

    LinearTransition accumulated;
    bool first = true;

    for (size_t i = 0; i < lines.size(); ++i) {
        const UltimateTransitionLine& line = lines[i];

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "\nProcessing transition " << (i + 1) << "/" << lines.size()
                    << " (label: " << line.label << ")" << std::endl;
        }

        LinearTransition current_trans;

        current_trans.var_to_ssa_in = line.in_vars;
        current_trans.var_to_ssa_out = line.out_vars;

        if (VERBOSITY == VerbosityLevel::VERBOSE) {
            std::cout << "  InVars: ";
            for (const auto& [var, ssa] : line.in_vars) {
                std::cout << var << "=" << ssa << " ";
            }
            std::cout << std::endl;

            std::cout << "  OutVars: ";
            for (const auto& [var, ssa] : line.out_vars) {
                std::cout << var << "=" << ssa << " ";
            }
            std::cout << std::endl;
        }

        // Add polyhedra from the DNF
        if (!line.dnf.polyhedra.empty()) {
            for (const auto& poly : line.dnf.polyhedra) {
                current_trans.addPolyhedron(poly);
            }
        } else if (line.formula == "true" || line.formula.empty()) {
            current_trans.addPolyhedron({});
        } else {
            std::cerr << "  Warning: Formula not parsed: " << line.formula << std::endl;
        }

        // Sequential composition
        if (first) {
            accumulated = current_trans;
            first = false;
        } else {
            if (VERBOSITY == VerbosityLevel::VERBOSE) {
                std::cout << "  Composing with accumulated transition..." << std::endl;
            }
            accumulated = accumulated.compose(current_trans);
        }
    }

    if (VERBOSITY == VerbosityLevel::VERBOSE) {
        std::cout << "\n--- Transition building complete ---" << std::endl;
    }
    return accumulated;
}