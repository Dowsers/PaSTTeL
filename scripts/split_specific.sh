#! /bin/bash
# Classification d'un benchmark de lassos selon le contenu des fichiers .txt.
# UNKNOWN_LOOP est deplace (mv) — les autres classes sont copiees (cp).
# Un fichier UNKNOWN_LOOP n'entre dans aucune autre classe.
# OTHERS recoit les fichiers n'appartenant a aucune des 8 classes principales.
#
# Classes :
#   UNKNOWN_LOOP     — Loop TransFormula: N/A (loop feasibility: UNKNOWN)  [mv]
#   BOOLEAN_OP       — au moins une variable de type Bool
#   ARRAY_OP         — au moins une variable de type Array (simple ou imbrique)
#   REAL_VARS        — au moins une variable de type Real
#   FUNCT_SIGNATURE  — Function signatures non vide (pas "(none)")
#   UNDEF_TYPE       — au moins une variable d'un type non primitif
#                       (autre que Int, Real, Bool, (Array...))
#   SI_ARRAYS        — "Array index supporting invariants" non vides
#   ALL_INT_VARS     — toutes les variables sont Int, aucune signature de fonction
#   OTHERS           — aucune des classes ci-dessus
#
# Usage : lancer depuis le dossier racine du benchmark contenant les lasso_traces_*/

set -euo pipefail

CLASSES=(UNKNOWN_LOOP BOOLEAN_OP ARRAY_OP REAL_VARS FUNCT_SIGNATURE UNDEF_TYPE SI_ARRAYS ALL_INT_VARS OTHERS)

for cls in "${CLASSES[@]}"; do
    mkdir -p "$cls"
done

copy_to_class() {
    local cls="$1"
    local txt="$2"          # chemin relatif : lasso_traces_X/lasso_trace_Y.txt
    mkdir -p "$cls/$(dirname "$txt")"
    cp "$txt" "$cls/$txt"
}

mv_to_class() {
    local cls="$1"
    local txt="$2"
    mkdir -p "$cls/$(dirname "$txt")"
    mv "$txt" "$cls/$txt"
}

# Collecte tous les fichiers a traiter
mapfile -t FILES < <(find . -maxdepth 2 -path './lasso_traces_*/*.txt' -name 'lasso_trace_*.txt' | sort)

total=${#FILES[@]}
echo "Classifying $total files..."

# Awk lit chaque fichier une seule fois et emet les classes applicables sur stdout.
# Format de sortie : "CLASSE\tchemin/relatif"
# UNKNOWN_LOOP est emis en premier et marque "skip" pour les autres classes.
awk '
FNR == 1 {
    # New file: reset state
    file = FILENAME
    sub(/^\.\//, "", file)

    in_vars      = 0
    in_fsig      = 0
    in_si_header = 0

    has_bool     = 0
    has_array    = 0
    has_real     = 0
    has_undef    = 0
    has_fsig     = 0
    has_si       = 0
    unknown_loop = 0
    all_int      = 1
    any_var      = 0
}

/^Loop TransFormula: N\/A \(loop feasibility: UNKNOWN\)/ {
    unknown_loop = 1
}

/^Variables:/        { in_vars = 1; in_fsig = 0; next }
/^Function signatures:/ { in_vars = 0; in_fsig = 1; next }
/^---/               { in_vars = 0; in_fsig = 0; in_si_header = 0 }

in_vars && /:[[:space:]]+[^[:space:]]/ {
    match($0, /:[[:space:]]+(.+)$/, arr)
    typ = arr[1]
    gsub(/[[:space:]]+$/, "", typ)
    any_var = 1

    if      (typ == "Bool")      { has_bool  = 1; all_int = 0 }
    else if (typ ~ /^\(Array/)   { has_array = 1; all_int = 0 }
    else if (typ == "Real")      { has_real  = 1; all_int = 0 }
    else if (typ != "Int")       { has_undef = 1; all_int = 0 }
}

in_fsig && /[^[:space:]]/ && !/\(none\)/ { has_fsig = 1; all_int = 0 }

/[Aa]rray index supporting invariants:/ { in_si_header = 1 }
in_si_header && /\[/ { has_si = 1; in_si_header = 0 }

ENDFILE {
    # UNKNOWN_LOOP : deplace, ne participe pas aux autres classes
    if (unknown_loop) {
        print "UNKNOWN_LOOP\t" file
    } else if (has_array) {
        # Arrays + quoi que ce soit => ARRAY_OP uniquement
        print "ARRAY_OP\t" file
    } else if (has_fsig) {
        # Signatures de fonctions + autres types (bool/int/real/undef) => FUNCT_SIGNATURE uniquement
        print "FUNCT_SIGNATURE\t" file
    } else {
        classified = 0
        if (has_bool)           { print "BOOLEAN_OP\t"   file; classified = 1 }
        if (has_real)           { print "REAL_VARS\t"    file; classified = 1 }
        if (has_undef)          { print "UNDEF_TYPE\t"   file; classified = 1 }
        if (has_si)             { print "SI_ARRAYS\t"    file; classified = 1 }
        if (any_var && all_int) { print "ALL_INT_VARS\t" file; classified = 1 }

        if (!classified) print "OTHERS\t" file
    }
}
' "${FILES[@]}" | while IFS=$'\t' read -r cls txt; do
    if [ "$cls" = "UNKNOWN_LOOP" ]; then
        mv_to_class "$cls" "$txt"
    else
        copy_to_class "$cls" "$txt"
    fi
done

echo ""
echo "=== Classification terminee ==="
for cls in "${CLASSES[@]}"; do
    count=$(find "$cls" -name 'lasso_trace_*.txt' 2>/dev/null | wc -l)
    printf "  %-20s : %d fichier(s)\n" "$cls" "$count"
done
