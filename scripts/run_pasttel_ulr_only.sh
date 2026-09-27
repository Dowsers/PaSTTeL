#!/bin/bash
# run_pasttel_ulr_only.sh -- Run P-ULR (PaSTTeL alone) on lasso programs in JSON format.
#
# No Ultimate involved: PaSTTeL analyses JSON lassos that Ultimate already extracted, such as the
# pre-extracted smoke-test lassos or the unit-test examples shipped with PaSTTeL.
#
# Input resolution:
#   --input <file.json>   that single file
#   --input <directory>   every .json found under it, grouped by directory
#   (no --input)          pasttel/examples/ (PaSTTeL's own examples)
#
# Usage:
#   bash scripts/run_pasttel_ulr_only.sh [--input <file|dir>]
#                                        [--solver z3|cvc5]                      (default: z3)
#                                        [--cpus <int>]                          (default: 1, sequential)
#                                        [--timeout <sec>]                       (default: 600)
#                                        [--strat both|terminate|nonterminate]  (default: both)
#                                        [--output <log>]                        (default: output/pasttel-ulr/pasttel_ulr_results.log)
#
# The paper uses Z3 only; --solver cvc5 is kept for anyone who wants to try PaSTTeL with CVC5.
#
# Environment overrides: APP_DIR, PASTTEL_BIN.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

INPUT=""
SOLVER=z3
CPUS=1
TIMEOUT=600
STRAT=both
OUTPUT_LOG="${APP_DIR}/output/pasttel-ulr/pasttel_ulr_results.log"

usage() { sed -n "2,$(grep -n '^# Environment overrides' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)   INPUT="$2";      shift 2 ;;
        --solver)  SOLVER="$2";     shift 2 ;;
        --cpus)    CPUS="$2";       shift 2 ;;
        --timeout) TIMEOUT="$2";    shift 2 ;;
        --strat)   STRAT="$2";      shift 2 ;;
        --output)  OUTPUT_LOG="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

case "${SOLVER}" in z3|cvc5) ;; *) die "--solver must be z3 or cvc5, got '${SOLVER}'" ;; esac
case "${STRAT}" in both|terminate|nonterminate) ;; *) die "--strat must be both, terminate or nonterminate" ;; esac
require_file "${PASTTEL_BIN}" "pasttel binary"
[ -x "${PASTTEL_BIN}" ] || die "${PASTTEL_BIN} is not executable (run 'make -j' in ${PASTTEL_HOME})"
if [ -z "${INPUT}" ]; then
    INPUT="${PASTTEL_HOME}/examples"
    echo "No --input given: using PaSTTeL's examples in ${INPUT}"
fi
require_inputs "${INPUT}"

LOG_DIR="$(dirname "${OUTPUT_LOG}")"
mkdir -p "${LOG_DIR}"
rm -f "${OUTPUT_LOG}"

echo "============================================================"
echo " P-ULR -- standalone run"
echo "============================================================"
echo " Input   : ${INPUT}"
echo " pasttel : ${PASTTEL_BIN}"
echo " Solver  : ${SOLVER}"
echo " CPUs    : ${CPUS}"
echo " Timeout : ${TIMEOUT}s per lasso"
echo " Strategy: ${STRAT}"
echo " Output  : ${OUTPUT_LOG}"
echo "============================================================"
echo ""

# run_one <json> <log>  -- analyse one lasso, print one line, append PaSTTeL's report to <log>.
run_one() {
    local json="$1" log="$2" out rc verdict secs
    rc=0
    out=$(timeout "${TIMEOUT}" "${PASTTEL_BIN}" -a "${STRAT}" -s "${SOLVER}" -c "${CPUS}" \
          -t "${TIMEOUT}" "${json}" 2>&1) || rc=$?
    verdict=$(sed -n 's/^OVERALL RESULT:[[:space:]]*//p' <<< "${out}" | tail -1)
    secs=$(sed -n 's/^TOTAL TIME:[[:space:]]*\([0-9.]*\).*/\1/p' <<< "${out}" | tail -1)
    # No report line means PaSTTeL did not finish: killed by the timeout (exit 124) or crashed.
    if [ -z "${verdict}" ]; then
        if [ "${rc}" -eq 124 ]; then verdict="TIMEOUT"; else verdict="ERROR (exit ${rc})"; fi
    fi
    if [ -n "${secs}" ]; then
        secs=$(awk -v s="${secs}" 'BEGIN { printf "%.2f ms", s * 1000 }')
    else
        secs="-"
    fi
    printf '    %-55s -> %-20s | %s\n' "$(basename "${json}")" "${verdict}" "${secs}"
    { echo "=== ${json}"; echo "${out}"; echo; } >> "${log}"
}

if [ -f "${INPUT}" ]; then
    run_one "$(realpath "${INPUT}")" "${OUTPUT_LOG}"
    count=1
else
    count=0
    # One group per directory holding JSON files, each with its own log next to the main one.
    while IFS= read -r -d '' dir; do
        label="${dir#"$(realpath "${INPUT}")"/}"
        [ "${label}" = "${dir}" ] && label="$(basename "${dir}")"
        group_log="${LOG_DIR}/$(tr '/' '_' <<< "${label}").pasttel.log"
        rm -f "${group_log}"
        echo "  -> ${label}"
        while IFS= read -r -d '' json; do
            run_one "${json}" "${group_log}"
            count=$((count + 1))
        done < <(find "${dir}" -maxdepth 1 -name '*.json' -type f -print0 | sort -z)
        cat "${group_log}" >> "${OUTPUT_LOG}"
    done < <(find "$(realpath "${INPUT}")" -name '*.json' -type f -printf '%h\0' | sort -zu)
fi

echo ""
echo "Processed ${count} JSON file(s). Full PaSTTeL reports: ${OUTPUT_LOG}"
echo "============================================================"
