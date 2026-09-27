#!/bin/bash
# run_full_evaluation.sh -- The paper's three comparisons, on the full benchmark by default.
#
#   fixed     ULR (tools/UAutomizer-linux, fixed strategy order)        vs P-ULR-Seq and P-ULR-Par<N>
#   shuffled  ULR (tools/UAutomizer-linux-shuffler, random order)       vs P-ULR-Seq and P-ULR-Par<N>
#   upl       ULR (tools/UAutomizer-PaSTTeL-linux, LassoRanker backend) vs UPL (same release, PaSTTeL backend)
#
# fixed and shuffled compare per lasso trace and run Ultimate with its default settings
# (scripts/run_pulr.sh). upl compares per program and is the only part driven by settings files,
# tools/settings/*.epf (scripts/run_ulr_vs_upl.sh). Only Z3 is used; no CVC* run is part of it.
# scripts/run_smoke_test.sh is this script on 10 small programs with short timeouts.
#
# Usage:
#   bash scripts/run_full_evaluation.sh [--input <dir|file>]...       (repeatable; default: benchmarks/C and benchmarks/BPL)
#                                       [--output <dir>]              (default: output/full)
#                                       [--parts <list>]              (default: fixed,shuffled,upl)
#                                       [--ultimate-timeout <sec>]    (default: 3000, per Ultimate run in fixed, shuffled)
#                                       [--upl-timeout <sec>]         (default: 1000, per Ultimate run in upl)
#                                       [--pasttel-timeout <sec>]     (default: 600, per PaSTTeL run on one trace)
#                                       [--upl-pasttel-timeout <sec>] (default: 20, PaSTTeL budget per lasso inside UPL)
#
# The defaults are the paper's; each is defined once, in scripts/common.sh.
#                                       [--par-cpus <int>]            (default: 7)
#
# Each part runs Ultimate once per program (upl twice), each run capped by its timeout; the banner
# prints the resulting worst case. PaSTTeL is comparatively cheap: on the paper's 9084 traces,
# P-ULR-Seq took 0.48 h in total.
#
# Exits non-zero if any selected part produced no result, so that a failure cannot go unnoticed
# (the Docker build relies on this through the smoke test).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

declare -a INPUTS=()
OUTPUT_DIR="${APP_DIR}/output/full"
PARTS="fixed,shuffled,upl"
ULTIMATE_TIMEOUT="${PULR_ULTIMATE_TIMEOUT_DEFAULT}"
UPL_TIMEOUT="${UPL_ULTIMATE_TIMEOUT_DEFAULT}"
PASTTEL_TIMEOUT="${PULR_PASTTEL_TIMEOUT_DEFAULT}"
UPL_PASTTEL_TIMEOUT="${UPL_PASTTEL_TIMEOUT_DEFAULT}"
PAR_CPUS=7
# Banner only; run_smoke_test.sh sets it through the environment.
TITLE="${EVALUATION_TITLE:-full evaluation}"

usage() { sed -n "2,$(grep -n '^# Exits non-zero' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)               INPUTS+=("$2");          shift 2 ;;
        --output)              OUTPUT_DIR="$2";         shift 2 ;;
        --parts)               PARTS="$2";              shift 2 ;;
        --ultimate-timeout)    ULTIMATE_TIMEOUT="$2";   shift 2 ;;
        --upl-timeout)         UPL_TIMEOUT="$2";        shift 2 ;;
        --pasttel-timeout)     PASTTEL_TIMEOUT="$2";    shift 2 ;;
        --upl-pasttel-timeout) UPL_PASTTEL_TIMEOUT="$2"; shift 2 ;;
        --par-cpus)            PAR_CPUS="$2";           shift 2 ;;
        -h|--help)             usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

[ ${#INPUTS[@]} -gt 0 ] || INPUTS=("${APP_DIR}/benchmarks/C" "${APP_DIR}/benchmarks/BPL")
require_inputs "${INPUTS[@]}"

# Validate --parts before spending hours on the first one.
declare -A RUN=()
IFS=',' read -r -a _parts <<< "${PARTS}"
for part in "${_parts[@]}"; do
    case "${part}" in
        fixed|shuffled|upl) RUN[${part}]=1 ;;
        *) die "unknown part '${part}' in --parts (expected fixed, shuffled, upl)" ;;
    esac
done

n_programs=0
while IFS= read -r -d '' _; do n_programs=$((n_programs + 1)); done < <(collect_programs "${INPUTS[@]}")
[ "${n_programs}" -gt 0 ] || die "no .c/.bpl program found in: ${INPUTS[*]}"
# Worst case for Ultimate: one run per program per P-ULR part, two per program for upl.
worst_s=$(( (${RUN[fixed]:-0} + ${RUN[shuffled]:-0}) * n_programs * ULTIMATE_TIMEOUT \
            + 2 * ${RUN[upl]:-0} * n_programs * UPL_TIMEOUT ))

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"
OUT_FIXED="${OUTPUT_DIR}/ulr_fixed_order"
OUT_SHUFFLED="${OUTPUT_DIR}/ulr_shuffled_order"
OUT_UPL="${OUTPUT_DIR}/ulr_vs_upl"
PAR_LABEL="P-ULR-Par${PAR_CPUS}"

declare -a INPUT_ARGS=()
for in in "${INPUTS[@]}"; do INPUT_ARGS+=(--input "${in}"); done

echo "============================================================"
echo " PaSTTeL artifact -- ${TITLE}"
echo "============================================================"
echo " Programs         : ${n_programs} (from ${INPUTS[*]})"
echo " Parts            : ${PARTS}"
echo " Ultimate timeout : ${ULTIMATE_TIMEOUT}s per run (fixed, shuffled), ${UPL_TIMEOUT}s per run (upl)"
echo " PaSTTeL timeout  : ${PASTTEL_TIMEOUT}s per trace (fixed, shuffled), ${UPL_PASTTEL_TIMEOUT}s per lasso (upl)"
echo " P-ULR configs    : P-ULR-Seq (1 cpu), ${PAR_LABEL} (${PAR_CPUS} cpus), Z3"
echo " Worst case       : $(awk -v s="${worst_s}" 'BEGIN { printf "%.1f h", s / 3600 }') for Ultimate," \
     "if every run hit its timeout, plus PaSTTeL"
echo " Output           : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

if [ -n "${RUN[fixed]:-}" ]; then
    echo "################ ULR (UAutomizer-linux, fixed order) vs P-ULR ################"
    bash "${SCRIPT_DIR}/run_pulr.sh" --ultimate-home "${ULTIMATE_ULR}" "${INPUT_ARGS[@]}" \
        --output "${OUT_FIXED}" --ultimate-timeout "${ULTIMATE_TIMEOUT}" \
        --pasttel-timeout "${PASTTEL_TIMEOUT}" --par-cpus "${PAR_CPUS}"
    echo ""
fi

if [ -n "${RUN[shuffled]:-}" ]; then
    echo "################ ULR (UAutomizer-linux-shuffler, random order) vs P-ULR ################"
    bash "${SCRIPT_DIR}/run_pulr.sh" --ultimate-home "${ULTIMATE_ULR_SHUFFLE}" "${INPUT_ARGS[@]}" \
        --output "${OUT_SHUFFLED}" --ultimate-timeout "${ULTIMATE_TIMEOUT}" \
        --pasttel-timeout "${PASTTEL_TIMEOUT}" --par-cpus "${PAR_CPUS}"
    echo ""
fi

if [ -n "${RUN[upl]:-}" ]; then
    echo "################ ULR vs UPL (UAutomizer-PaSTTeL-linux, settings files) ################"
    # Both sides from the same release, so that only the rank-synthesis backend differs; an exported
    # ULTIMATE_ULR would make run_ulr_vs_upl.sh take the baseline from another release.
    ULTIMATE_ULR="" bash "${SCRIPT_DIR}/run_ulr_vs_upl.sh" "${INPUT_ARGS[@]}" --output "${OUT_UPL}" \
        --timeout "${UPL_TIMEOUT}" --pasttel-cpus "${PAR_CPUS}" --pasttel-timeout "${UPL_PASTTEL_TIMEOUT}"
    echo ""
fi

# -- Verdict ---------------------------------------------------------------------
rows() { if [ -s "$1" ]; then echo $(( $(wc -l < "$1") - 1 )); else echo 0; fi; }

status=0
check() {
    local label="$1" csv="$2" n
    n=$(rows "${csv}")
    if [ "${n}" -gt 0 ]; then
        printf '  OK      %-44s %6d row(s)  %s\n' "${label}" "${n}" "${csv#"${OUTPUT_DIR}"/}"
    else
        printf '  FAILED  %-44s no result     %s\n' "${label}" "${csv#"${OUTPUT_DIR}"/}"
        status=1
    fi
}

echo "============================================================"
echo " Summary -- ${TITLE}"
echo "============================================================"
if [ -n "${RUN[fixed]:-}" ]; then
    check "ULR (fixed order)    vs P-ULR-Seq"    "${OUT_FIXED}/results_P-ULR-Seq_z3.csv"
    check "ULR (fixed order)    vs ${PAR_LABEL}" "${OUT_FIXED}/results_${PAR_LABEL}_z3.csv"
fi
if [ -n "${RUN[shuffled]:-}" ]; then
    check "ULR (shuffled order) vs P-ULR-Seq"    "${OUT_SHUFFLED}/results_P-ULR-Seq_z3.csv"
    check "ULR (shuffled order) vs ${PAR_LABEL}" "${OUT_SHUFFLED}/results_${PAR_LABEL}_z3.csv"
fi
if [ -n "${RUN[upl]:-}" ]; then
    check "ULR vs UPL (per program)"             "${OUT_UPL}/results_ULR_vs_UPL.csv"
fi
echo ""
for d in "${OUT_FIXED}" "${OUT_SHUFFLED}" "${OUT_UPL}"; do
    [ -f "${d}/summary_tables.log" ] && echo "  Tables : ${d}/summary_tables.log"
done
echo "  Plots  : ${OUTPUT_DIR}/*/results_*.html"
echo "============================================================"
if [ "${status}" -ne 0 ]; then
    echo "${TITLE^^} FAILED: see the FAILED lines above and the logs next to each CSV." >&2
fi
exit "${status}"
