#!/bin/bash


dir=$1
csv=$2
ext=$3
loop_lasso=$4

for f in $dir/lass*$ext ; 
do 
	python3 scripts/benchmark_ultimate_vs_terminator.py --input-dir $f --terminator-bin ./bin/terminator --output $csv --check $loop_lasso --cpus 2 --timeout 120 --plot
done;
