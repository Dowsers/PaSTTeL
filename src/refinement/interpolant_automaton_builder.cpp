#include <iostream>
#include <cassert>
#include <spot/twaalgos/hoa.hh>
#include <spot/twa/bddprint.hh>

#include "refinement/interpolant_automaton_builder.h"

// ============================================================================
// CONSTRUCTEUR
// ============================================================================

InterpolantAutomatonBuilder::InterpolantAutomatonBuilder(spot::bdd_dict_ptr dict)
    : mDict(dict) {
}

// ============================================================================
// CONSTRUCTION DE L'AUTOMATE
// ============================================================================

spot::twa_graph_ptr InterpolantAutomatonBuilder::build(
    const PredicateEncoder::Predicates& predicates,
    const LassoProgram& lasso)
{
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Construction Automate d'Interpolants (Refine_ω)     ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝" << std::endl;

    // Créer l'automate SPOT
    auto aut = spot::make_twa_graph(mDict);
    
    // Propositions atomiques pour les labels
    bdd stem_label = registerAP("stem");
    bdd loop_label = registerAP("loop");
    
    std::cout << "  ✓ Dictionnaire BDD initialisé" << std::endl;
    std::cout << "  ✓ Propositions atomiques: stem, loop" << std::endl;

    // ─────────────────────────────────────────────────────────────────────
    // CAS 1 : Stem vide → Honda est l'état initial ET acceptant
    // ─────────────────────────────────────────────────────────────────────
    
    if (lasso.stem.polyhedra.empty()) {
        std::cout << "\n[1] Stem vide détecté" << std::endl;
        
        // Un seul état : honda (initial + acceptant)
        unsigned q_honda = aut->new_state();
        aut->set_init_state(q_honda);
        
        // Marquer comme acceptant (Büchi acceptance: Inf(0))
        aut->set_buchi();
        aut->new_edge(q_honda, q_honda, loop_label, {0});  // {0} = accepting
        
        // Attacher le prédicat honda
        attachPredicate(aut, q_honda, predicates.honda_predicate);
        
        std::cout << "  ✓ 1 état créé (q" << q_honda << ": initial + acceptant)" << std::endl;
        std::cout << "  ✓ 1 transition self-loop" << std::endl;
    }
    
    // ─────────────────────────────────────────────────────────────────────
    // CAS 2 : Stem non-vide → 2 états (q_init, q_honda)
    // ─────────────────────────────────────────────────────────────────────
    
    else {
        std::cout << "\n[1] Stem non-vide" << std::endl;
        
        // État initial : stem_precondition
        unsigned q_init = aut->new_state();
        aut->set_init_state(q_init);
        attachPredicate(aut, q_init, predicates.stem_precondition);
        
        // État acceptant : honda
        unsigned q_honda = aut->new_state();
        attachPredicate(aut, q_honda, predicates.honda_predicate);
        
        // Marquer l'acceptance Büchi
        aut->set_buchi();
        
        // Transitions
        aut->new_edge(q_init, q_honda, stem_label);           // q_init --[stem]--> q_honda
        aut->new_edge(q_honda, q_honda, loop_label, {0});    // q_honda --[loop]--> q_honda (accepting)
        
        std::cout << "  ✓ 2 états créés (q" << q_init << ", q" << q_honda << ")" << std::endl;
        std::cout << "  ✓ q" << q_init << " --[stem]--> q" << q_honda << std::endl;
        std::cout << "  ✓ q" << q_honda << " --[loop]--> q" << q_honda << " (accepting)" << std::endl;
    }

    std::cout << "\n  Automate d'interpolants SPOT construit" << std::endl;
    
    return aut;
}

// ============================================================================
// UTILITAIRES
// ============================================================================

bdd InterpolantAutomatonBuilder::registerAP(const std::string& name) {
    // Enregistrer la proposition atomique et obtenir son BDD
    int var = mDict->register_proposition(spot::formula::ap(name), nullptr);
    return bdd_ithvar(var);
}

void InterpolantAutomatonBuilder::attachPredicate(
    spot::twa_graph_ptr& aut,
    unsigned state_id,
    const std::string& predicate)
{
    // Stocker le prédicat SMT dans les propriétés de l'état (state_name)
    aut->set_named_prop("state-" + std::to_string(state_id) + "-predicate",
                        new std::string(predicate));
}

void InterpolantAutomatonBuilder::printAutomaton(const spot::twa_graph_ptr& aut) const {
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  AUTOMATE D'INTERPOLANTS (Büchi SPOT)               ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝" << std::endl;

    std::cout << "\n[STATISTIQUES]" << std::endl;
    std::cout << "  Nombre d'états: " << aut->num_states() << std::endl;
    std::cout << "  Nombre de transitions: " << aut->num_edges() << std::endl;
    std::cout << "  État initial: " << aut->get_init_state_number() << std::endl;
    
    std::cout << "\n[ÉTATS]" << std::endl;
    for (unsigned s = 0; s < aut->num_states(); ++s) {
        std::cout << "  q" << s;
        if (s == aut->get_init_state_number()) {
            std::cout << " (INITIAL)";
        }
        std::cout << std::endl;
        
        // Récupérer le prédicat via get_named_prop
        auto pred_ptr = aut->get_named_prop<std::string>("state-" + std::to_string(s) + "-predicate");
        if (pred_ptr) {
            std::string pred = *pred_ptr;
            if (pred.length() > 80) {
                std::cout << "    Prédicat: " << pred.substr(0, 77) << "..." << std::endl;
            } else {
                std::cout << "    Prédicat: " << pred << std::endl;
            }
        }
    }

    std::cout << "\n[TRANSITIONS]" << std::endl;
    for (unsigned s = 0; s < aut->num_states(); ++s) {
        for (auto& t : aut->out(s)) {
            std::cout << "  q" << s << " --["
                      << spot::bdd_format_set(mDict, t.cond)
                      << "]--> q" << t.dst;
            
            // Vérifier acceptance
            if (t.acc != spot::acc_cond::mark_t()) {
                std::cout << " (accepting)";
            }
            std::cout << std::endl;
        }
    }
    
    std::cout << "\n[FORMAT HOA]" << std::endl;
    // spot::print_hoa(std::cout, aut);
}