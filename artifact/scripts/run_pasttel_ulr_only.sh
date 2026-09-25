#!/bin/bash
# run_pasttel_ulr_only.sh — Run P-ULR on lasso trace JSON files.
#
# Input resolution (in order of priority):
#   1. --input <file.json>   → run on that single file
#   2. --input <directory>   → run on all .json files recursively in that directory
#   3. (no --input)          → run on /app/pasttel/examples/ (unit test suite, 53 examples)
#
# Usage:
#   bash /app/scripts/run_pasttel_ulr_only.sh [--input <file|dir>]
#                                          [--solver z3|cvc5]  (default: z3)
#                                          [--cpus N]          (default: 1)
#                                          [--timeout <sec>]   (default: 600)
#                                          [--strat both|terminate|nonterminate] (default: both)
#                                          [--output <log>]    (default: /app/output/pasttel-ulr/pasttel_ulr_results.log)



set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASTTEL_HOME="${PASTTEL_HOME:-/app/pasttel}"
APP_DIR="$(dirname "$SCRIPT_DIR")"
INPUT=""
SOLVER=z3
CPUS=1
TIMEOUT=600
STRAT=both
OUTPUT_LOG="${APP_DIR}/output/pasttel-ulr/pasttel_ulr_results.log"
PASTTEL_BIN="${PASTTEL_HOME}/bin/pasttel"

function help(){
	echo "Usage:    bash $0 [--input <file|dir>]
	                        [--solver z3|cvc5]  (default: z3)
	                        [--cpus N]          (default: 1)
	                        [--timeout <sec>]   (default: 600)
	                        [--strat both|terminate|nonterminate] (default: both)
	                        [--output <log>]    (default: ${APP_DIR}/output/pasttel_ulr_results.log)
	                        "
}


while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)   INPUT="$2";   shift 2 ;;
        --solver)  SOLVER="$2";  shift 2 ;;
        --cpus)    CPUS="$2";    shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --strat)   STRAT="$2";   shift 2 ;;
        --output)  OUTPUT_LOG="$2";  shift 2 ;;
        -h|--help) help; exit 1;;
        *) echo "Unknown option: $1";  help ; exit 1 ;;
    esac
done


# Resolve input
if [ -z "$INPUT" ]; then
    INPUT="${PASTTEL_HOME}/examples"
    echo "No --input specified. Using built-in examples: ${INPUT}"
fi

echo "============================================================"
echo " P-ULR — standalone run"
echo "============================================================"
echo " Input   : ${INPUT}"
echo " Solver  : ${SOLVER}"
echo " CPUs    : ${CPUS}"
echo " Timeout : ${TIMEOUT}s"
echo " Strategy: ${STRAT}"
echo " Output  : ${OUTPUT_LOG}"
echo "============================================================"
echo " Delete old ${OUTPUT_LOG} directory..."
echo "============================================================"
echo ""

LOG_DIR="$(dirname "$OUTPUT_LOG")"
rm -f "${OUTPUT_LOG}"
mkdir -p "${LOG_DIR}"

run_one() {
    local json="$1"
    local log="$2"
    local result
    result=$(timeout "$TIMEOUT" "${PASTTEL_BIN}" -a "${STRAT}" -s "${SOLVER}" -c "${CPUS}" "${json}" 2>&1 || true)
    local verdict
    verdict=$(echo "$result" | grep -oE 'TERMINATING|NON-TERMINATING|TIMEOUT|UNKNOWN' | tail -1 || echo "UNKNOWN")
    local elapsed
    elapsed=$(echo "$result" | grep 'TOTAL TIME' | tail -1 | cut -d':' -f2 | tr -d ' s' || echo "0.00")
    local time_str
    time_str=$(awk "BEGIN {printf \"%.2fms\", $elapsed * 1000.0}")
    printf "    %-55s → %-20s|  %s\n" "$(basename "$json")" "${verdict}" "${time_str}"
    echo "$result" >> "${log}"
}

run_group() {
    local dir="$1"
    local label="${dir#$INPUT/}"
    [ "$label" = "$dir" ] && label="$(basename "$dir")"
    local trace_files=()
    while IFS= read -r -d '' f; do
        trace_files+=("$f")
    done < <(find "$dir" -maxdepth 1 -name "*.json" -type f -print0 | sort -z)
    [ ${#trace_files[@]} -eq 0 ] && return
    local group_log="${LOG_DIR}/${label}.pasttel.log"
    rm -f "${group_log}"
    echo "  → ${label}"
    echo "    ${#trace_files[@]} trace(s) — P-ULR results:"
    for json in "${trace_files[@]}"; do
        run_one "$json" "${group_log}"
    done
}

if [ -f "$INPUT" ]; then
    run_one "$INPUT" "${OUTPUT_LOG}"
elif [ -d "$INPUT" ]; then
    count=0
    while IFS= read -r dir; do
        run_group "$dir"
        count=$((count + $(find "$dir" -maxdepth 1 -name "*.json" -type f | wc -l)))
    done < <(find "$INPUT" -name "*.json" -type f -print0 \
             | xargs -0 -I{} dirname {} \
             | sort -u)
    echo "Processed ${count} JSON file(s)."
else
    echo "ERROR: --input '${INPUT}' is neither a file nor a directory."
    exit 1
fi

echo ""
echo "============================================================"
