#!/bin/bash

rm *.log
rm result_atva26*

echo "----------------- RUN SEQUENTIAL Z3 EXP -----------------"
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_sequential_normal_z3.csv bpl lasso both normal 1 z3 600 >> output_seq_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_sequential_normal_z3.csv c lasso both normal 1 z3 600 >> output_seq_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_sequential_normal_z3.csv bpl lasso both normal 1 z3 600 >> output_seq_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_sequential_normal_z3.csv c lasso both normal 1 z3 600 >> output_seq_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_sequential_normal_z3.csv bpl lasso both normal 1 z3 600 >> output_seq_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_sequential_normal_z3.csv c lasso both normal 1 z3 600 >> output_seq_atva26_z3.log

echo "----------------- RUN PARALLEL4 Z3 EXP -----------------"
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_parallel4_normal_z3.csv bpl lasso both normal 4 z3 600 >> output_para4_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_parallel4_normal_z3.csv c lasso both normal 4 z3 600 >> output_para4_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_parallel4_normal_z3.csv bpl lasso both normal 4 z3 600 >> output_para4_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_parallel4_normal_z3.csv c lasso both normal 4 z3 600 >> output_para4_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_parallel4_normal_z3.csv bpl lasso both normal 4 z3 600 >> output_para4_atva26_z3.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_parallel4_normal_z3.csv c lasso both normal 4 z3 600 >> output_para4_atva26_z3.log

echo "----------------- RUN SEQUENTIAL CVC5 EXP -----------------"
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_sequential_normal_cvc5.csv bpl lasso both normal 1 cvc5 600 >> output_seq_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_sequential_normal_cvc5.csv c lasso both normal 1 cvc5 600 >> output_seq_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_sequential_normal_cvc5.csv bpl lasso both normal 1 cvc5 600 >> output_seq_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_sequential_normal_cvc5.csv c lasso both normal 1 cvc5 600 >> output_seq_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_sequential_normal_cvc5.csv bpl lasso both normal 1 cvc5 600 >> output_seq_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_sequential_normal_cvc5.csv c lasso both normal 1 cvc5 600 >> output_seq_atva26_cvc5.log

echo "----------------- RUN PARALLEL4 CVC5 EXP -----------------"
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_parallel4_normal_cvc5.csv bpl lasso both normal 4 cvc5 600 >> output_para4_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/REAL_VARS/ result_atva26_parallel4_normal_cvc5.csv c lasso both normal 4 cvc5 600 >> output_para4_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_parallel4_normal_cvc5.csv bpl lasso both normal 4 cvc5 600 >> output_para4_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/BOOLEAN_OP/ result_atva26_parallel4_normal_cvc5.csv c lasso both normal 4 cvc5 600 >> output_para4_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_parallel4_normal_cvc5.csv bpl lasso both normal 4 cvc5 600 >> output_para4_atva26_cvc5.log
./scripts/comparison.sh ATVA2026_benchmark/ALL_INT_VARS/ result_atva26_parallel4_normal_cvc5.csv c lasso both normal 4 cvc5 600 >> output_para4_atva26_cvc5.log
