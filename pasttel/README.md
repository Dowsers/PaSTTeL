# PaSTTeL
Parallel analysiS framework for Termination and non-Termination of Lasso programs

The artifact's README (one directory up) explains how PaSTTeL is evaluated against Ultimate; this
file is about PaSTTeL alone.

# Build
Z3 (and CVC5) must be installed under a prefix with `include/`, `lib/` and `bin/`;
`../scripts/install.sh` installs both from `../tools/solvers/` into `solvers/`, then builds PaSTTeL.
By hand:
```
make -j$(nproc) PASTTEL=<solver prefix> CVC5_DIR=<solver prefix>   # bin/pasttel
make lib PASTTEL=<solver prefix> CVC5_DIR=<solver prefix>          # bin/libpasttel.a
```

# Test suite
```
python3 scripts/test_non_regression.py      # 247 tests, about 4 minutes
```

# Command line
```
./bin/pasttel [options] <lasso.json>        # ./bin/pasttel --help lists every option
  -a <terminate|nonterminate|both>   what to prove (default: both)
  -t <int>                           time limit, in seconds
  -s <z3|cvc5>                       SMT solver (default: z3)
  -c <int>                           cores the techniques race on (default: 1)
  -only <affine|nested|lexicographic|multiphase|piecewise>   one ranking template only
  -val                               re-check every synthesised ranking function
  -o <text|json>                     report format (default: text)
```
Example, the lasso of the paper's Figure 1:
```
./bin/pasttel -a both -s z3 -c 1 examples/figure1_paper_example.json
```

# Input
A lasso in JSON, its formulas in SMT-LIB syntax: `program_vars`, `var_types`, and the `stem` and
`loop` transitions, each with its `formula`, `in_vars` and `out_vars`. See `examples/` and
`include/parser/json_trace_parser.h`.

# Library
`bin/libpasttel.a`, entry point `runAnalysis()` in `include/pasttel.h`, on a `LassoProgram`
(`include/lasso_program.h`). A new technique implements `AnalysisInterface`
(`include/analysis_technique_interface.h`) and is registered with `addTechnique` in `runAnalysis()`
(`src/pasttel.cpp`).
