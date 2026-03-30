# PaSTTeL
Parallel analysiS framework for Termination and non-Termination of Lasso programs

# Build
make -j$(nproc)

# Testsuite
./scripts/test_non_regression.sh

./scripts/test_solver_regression.sh

# PaSTTel in Command Line
```
Usage: ./bin/pasttel [options] <filename>

Options:
  -t <terminate|nonterminate|both>   Set termination mode (default: both)
  -s <z3|cvc5>                      Set SMT solver (default: z3)
  -q                                Quiet mode (silence output)
  -v                                Verbose mode (more output)
  -c <int>                          Number of CPUs (default: 1)
  -h, --help                        Show this help message

Examples:
  ./bin/pasttel -t terminate -s z3 -c 4 input.json
  ./bin/pasttel -t both -s cvc5 -v input.json
```


