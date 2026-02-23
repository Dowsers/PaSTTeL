#! /bin/bash

grep -ri "Lasso termination:   NONTERMINATING" */*txt | cut -d':' -f1 > non_terminating.txt
grep -ri "Lasso termination:   TERMINATING" */*txt | cut -d':' -f1 > terminating.txt

for n in $(cat terminating.txt); do dir=$(dirname $n); mkdir -p TERM_LASSO/$dir; cp $n TERM_LASSO/$n ; done;
for n in $(cat non_terminating.txt); do dir=$(dirname $n); mkdir -p NON_TERM_LASSO/$dir; cp $n NON_TERM_LASSO/$n ; done;
