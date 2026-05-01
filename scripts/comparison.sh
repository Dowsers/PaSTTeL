#!/bin/bash


dir=$1
csv=$2
ext=$3		# c or bpl
loop_lasso=$4  # loop or lasso
strat=$5  # "terminate", "nonterminate" or "both	"
parse=$6  # "normal" or "preprocess"
cpus=$7
solver=$8 # "z3" or "cvc5"

for f in $dir/lass*$ext ;
do
	python3 scripts/benchmark_ultimate_vs_pasttel.py --input-dir $f --pasttel-bin ./bin/pasttel --output $csv --check $loop_lasso --cpus $cpus --solver $solver --timeout 120 --plot --log --strat $strat --parse $parse
done;
