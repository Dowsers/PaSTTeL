#ifndef INTERPOLANT_AUTOMATON_BUILDER_H
#define INTERPOLANT_AUTOMATON_BUILDER_H

#include <string>
#include <map>
#include <memory>
#include <spot/twa/twagraph.hh>
#include <spot/twa/bdddict.hh>

#include "refinement/predicate_encoder.h"
#include "lasso_program.h"

/**
 * @brief Constructeur d'automate d'interpolants pour Refine_ω
 * 
 * Construit un automate de Büchi SPOT depuis les prédicats encodés.
 * 
 * Structure de l'automate :
 *   - STEM : q_init --[stem]--> q_honda
 *   - LOOP : q_honda --[loop]--> q_honda (acceptant)
 * 
 * Référence: RefineBuchi.java::constructBuchiInterpolantAutomaton()
 */
class InterpolantAutomatonBuilder {
public:
    InterpolantAutomatonBuilder(spot::bdd_dict_ptr dict);

    /**
     * @brief Construit l'automate d'interpolants SPOT depuis les prédicats
     * 
     * @param predicates Prédicats encodés (stem_precondition, honda, etc.)
     * @param lasso Programme lasso (pour savoir si stem est vide)
     * @return Automate de Büchi SPOT
     */
    spot::twa_graph_ptr build(
        const PredicateEncoder::Predicates& predicates,
        const LassoProgram& lasso
    );

    /**
     * @brief Affiche l'automate SPOT (debug)
     */
    void printAutomaton(const spot::twa_graph_ptr& automaton) const;

    spot::bdd_dict_ptr get_dict() const {
        return mDict;
    };

private:
    spot::bdd_dict_ptr mDict;  // Dictionnaire partagé pour les BDD
    
    /**
     * @brief Enregistre une proposition atomique dans le dictionnaire
     * 
     * @param name Nom de la proposition (ex: "stem", "loop")
     * @return BDD correspondant
     */
    bdd registerAP(const std::string& name);
    
    /**
     * @brief Associe un prédicat SMT à un état (via propriétés SPOT)
     * 
     * @param aut Automate SPOT
     * @param state_id ID de l'état
     * @param predicate Formule SMT-LIB2
     */
    void attachPredicate(
        spot::twa_graph_ptr& aut,
        unsigned state_id,
        const std::string& predicate
    );
};

#endif // INTERPOLANT_AUTOMATON_BUILDER_H