#! /bin/bash




grep -ri "Lasso termination:   NONTERMINATING" */*txt | cut -d':' -f1 > non_terminating.txt
grep -ri "Lasso termination:   TERMINATING" */*txt | cut -d':' -f1 > terminating.txt

for n in $(cat terminating.txt); do dir=$(dirname $n); mkdir -p TERM_LASSO/$dir; cp $n TERM_LASSO/$n ; done;
for n in $(cat non_terminating.txt); do dir=$(dirname $n); mkdir -p NON_TERM_LASSO/$dir; cp $n NON_TERM_LASSO/$n ; done;

# Filter files with non-empty "Array index supporting invariants" into SI_ARRAYS

grep -ril "Array index supporting invariants:" TERM_LASSO/*/*txt > TERM_LASSO/si_arrays.txt
for n in $(cat TERM_LASSO/si_arrays.txt); do rel=${n#TERM_LASSO/}; mkdir -p TERM_LASSO/SI_ARRAYS/$(dirname $rel); mv $n TERM_LASSO/SI_ARRAYS/$rel ; done;

grep -ril "Array index supporting invariants:" NON_TERM_LASSO/*/*txt > NON_TERM_LASSO/si_arrays.txt
for n in $(cat NON_TERM_LASSO/si_arrays.txt); do rel=${n#NON_TERM_LASSO/}; mkdir -p NON_TERM_LASSO/SI_ARRAYS/$(dirname $rel); mv $n NON_TERM_LASSO/SI_ARRAYS/$rel ; done;
