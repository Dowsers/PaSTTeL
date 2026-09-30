#!/bin/bash
# run_full_evaluation.sh -- The paper's two comparisons, on the full benchmark by default.
#
#   pulr  ULR-Baseline (tools/UAutomizer-linux)                           vs P-ULR-Seq and P-ULR-Par<N>
#   upl   Ultimate-LR (tools/UAutomizer-PaSTTeL-linux, LassoRanker backend) vs Ultimate-PL (same release,
#         PaSTTeL backend)
#
# pulr compares per lasso trace and runs Ultimate with its default settings (scripts/run_ulr_vs_pulr.sh).
# upl compares per program (scripts/run_ulr_vs_upl.sh): Ultimate-LR runs with the same default settings,
# and Ultimate-PL with a settings file that only switches the rank-synthesis backend to PaSTTeL
# (tools/settings/BuchiAutomizerPasttel.epf.in). Only Z3 is used; no CVC* run is part of it.
# scripts/run_smoke_test.sh is this script on 10 small programs with short timeouts.
#
# Usage:
#   bash scripts/run_full_evaluation.sh [--subset]                    (benchmarks/subset, in about 3 hours; see below)
#                                       [--input <dir|file>]...       (repeatable; both parts; default: the paper's benchmark
#                                                                      of each part, benchmarks/ulr_vs_pulr for pulr,
#                                                                      benchmarks/ulr_vs_upl for upl)
#                                       [--output <dir>]              (default: output/full, output/subset with --subset)
#                                       [--parts <list>]              (default: pulr,upl)
#                                       [--ultimate-timeout <sec>]    (default: 3000, per Ultimate run in pulr)
#                                       [--upl-timeout <sec>]         (default: 1000, per Ultimate run in upl)
#
# The defaults are the paper's, each defined once in scripts/common.sh. PaSTTeL always runs with the
# paper's settings: 600 s per trace in pulr, on 1 and 7 cores; 20 s per lasso on 5 cores inside Ultimate-PL.
# scripts/run_ulr_vs_pulr.sh and scripts/run_ulr_vs_upl.sh, which this script runs, take other values.
#
# --subset replicates the comparisons in about 3 hours: pulr on the 100 programs of
# benchmarks/subset/ulr_vs_pulr, upl on the 100 of benchmarks/subset/ulr_vs_upl, each drawn at random
# from the paper's benchmark of that comparison (see their READMEs), with every Ultimate run capped
# at 300 s instead of 3,000 s (pulr) and 1,000 s (upl).
# PaSTTeL keeps the paper's timeouts. --ultimate-timeout, --upl-timeout, --output and --parts still
# apply; --input does not, since --subset sets the input.
#
# pulr runs Ultimate once per program and upl twice, each run capped by its timeout; the banner
# prints the resulting worst case. On the paper's benchmark, the P-ULR-Seq pass took about 20 minutes
# and the P-ULR-Par7 pass about 17.
#
# Exits non-zero if any selected part produced no result, so that a failure cannot go unnoticed
# (the Docker build relies on this through the smoke test).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

declare -a INPUTS=()
# Empty: the default, which depends on --subset, is filled in after the options are parsed.
OUTPUT_DIR=""
ULTIMATE_TIMEOUT=""
UPL_TIMEOUT=""
PARTS="pulr,upl"
SUBSET=false
# Banner only; run_smoke_test.sh sets it through the environment.
TITLE="${EVALUATION_TITLE:-}"

# --subset: its programs, its timeout for every Ultimate run, and its duration per part on the
# paper's machine, estimated from the paper's logs (README, section 3.2).
SUBSET_DIR="${APP_DIR}/benchmarks/subset"
SUBSET_ULTIMATE_TIMEOUT=300
declare -A SUBSET_HOURS=([pulr]=1 [upl]=2)

usage() { sed -n "2,$(grep -n '^# Exits non-zero' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --subset)              SUBSET=true;             shift ;;
        --input)               INPUTS+=("$2");          shift 2 ;;
        --output)              OUTPUT_DIR="$2";         shift 2 ;;
        --parts)               PARTS="$2";              shift 2 ;;
        --ultimate-timeout)    ULTIMATE_TIMEOUT="$2";   shift 2 ;;
        --upl-timeout)         UPL_TIMEOUT="$2";        shift 2 ;;
        -h|--help)             usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

# Validate --parts before spending hours on the first one.
declare -A RUN=()
IFS=',' read -r -a _parts <<< "${PARTS}"
for part in "${_parts[@]}"; do
    case "${part}" in
        pulr|upl) RUN[${part}]=1 ;;
        *) die "unknown part '${part}' in --parts (expected pulr, upl)" ;;
    esac
done

EXPECTED=""
# Without --input, each part runs on its own benchmark: the paper's, or with --subset its subset.
declare -a UPL_INPUTS=("${INPUTS[@]}")
if "${SUBSET}"; then
    [ ${#INPUTS[@]} -eq 0 ] || die "--subset runs on ${SUBSET_DIR}: use either --subset or --input"
    INPUTS=("${SUBSET_DIR}/ulr_vs_pulr")
    UPL_INPUTS=("${SUBSET_DIR}/ulr_vs_upl")
    ULTIMATE_TIMEOUT="${ULTIMATE_TIMEOUT:-${SUBSET_ULTIMATE_TIMEOUT}}"
    UPL_TIMEOUT="${UPL_TIMEOUT:-${SUBSET_ULTIMATE_TIMEOUT}}"
    # The estimate holds for the subset's own timeout only.
    if [ "${ULTIMATE_TIMEOUT}" = "${SUBSET_ULTIMATE_TIMEOUT}" ] && [ "${UPL_TIMEOUT}" = "${SUBSET_ULTIMATE_TIMEOUT}" ]; then
        hours=0
        for part in "${!RUN[@]}"; do hours=$((hours + ${SUBSET_HOURS[${part}]})); done
        EXPECTED="about ${hours} h on the paper's machine"
    fi
    OUTPUT_DIR="${OUTPUT_DIR:-${APP_DIR}/output/subset}"
    TITLE="${TITLE:-subset evaluation}"
fi
[ ${#INPUTS[@]} -gt 0 ] || INPUTS=("${APP_DIR}/benchmarks/ulr_vs_pulr")
[ ${#UPL_INPUTS[@]} -gt 0 ] || UPL_INPUTS=("${APP_DIR}/benchmarks/ulr_vs_upl")
ULTIMATE_TIMEOUT="${ULTIMATE_TIMEOUT:-${PULR_ULTIMATE_TIMEOUT_DEFAULT}}"
UPL_TIMEOUT="${UPL_TIMEOUT:-${UPL_ULTIMATE_TIMEOUT_DEFAULT}}"
OUTPUT_DIR="${OUTPUT_DIR:-${APP_DIR}/output/full}"
TITLE="${TITLE:-full evaluation}"
count_programs() { local n=0; while IFS= read -r -d '' _; do n=$((n + 1)); done < <(collect_programs "$@"); echo "${n}"; }
n_pulr=0; n_upl=0
if [ -n "${RUN[pulr]:-}" ]; then
    require_inputs "${INPUTS[@]}"
    n_pulr=$(count_programs "${INPUTS[@]}")
    [ "${n_pulr}" -gt 0 ] || die "no .c/.bpl program found in: ${INPUTS[*]}"
fi
if [ -n "${RUN[upl]:-}" ]; then
    require_inputs "${UPL_INPUTS[@]}"
    n_upl=$(count_programs "${UPL_INPUTS[@]}")
    [ "${n_upl}" -gt 0 ] || die "no .c/.bpl program found in: ${UPL_INPUTS[*]}"
fi
# Worst case for Ultimate: one run per program for pulr, two per program for upl.
worst_s=$(( n_pulr * ULTIMATE_TIMEOUT + 2 * n_upl * UPL_TIMEOUT ))

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"
OUT_PULR="${OUTPUT_DIR}/ulr_vs_pulr"
OUT_UPL="${OUTPUT_DIR}/ulr_vs_upl"
PAR_LABEL="P-ULR-Par${PULR_PAR_CPUS_DEFAULT}"

declare -a INPUT_ARGS=() UPL_INPUT_ARGS=()
for in in "${INPUTS[@]}"; do INPUT_ARGS+=(--input "${in}"); done
for in in "${UPL_INPUTS[@]}"; do UPL_INPUT_ARGS+=(--input "${in}"); done

echo "============================================================"
echo " PaSTTeL artifact -- ${TITLE}"
echo "============================================================"
if [ -n "${RUN[pulr]:-}" ]; then echo " Programs (pulr)  : ${n_pulr} (from ${INPUTS[*]})"; fi
if [ -n "${RUN[upl]:-}" ]; then echo " Programs (upl)   : ${n_upl} (from ${UPL_INPUTS[*]})"; fi
echo " Parts            : ${PARTS}"
echo " Ultimate timeout : ${ULTIMATE_TIMEOUT}s per run (pulr), ${UPL_TIMEOUT}s per run (upl)"
echo " PaSTTeL timeout  : ${PULR_PASTTEL_TIMEOUT_DEFAULT}s per trace (pulr), ${UPL_PASTTEL_TIMEOUT_DEFAULT}s per lasso (upl)"
echo " P-ULR configs    : P-ULR-Seq (1 cpu), ${PAR_LABEL} (${PULR_PAR_CPUS_DEFAULT} cpus), Z3"
echo " Ultimate-PL      : PaSTTeL on ${UPL_PASTTEL_CPUS_DEFAULT} cpus"
echo " Worst case       : $(awk -v s="${worst_s}" 'BEGIN { printf "%.1f h", s / 3600 }') for Ultimate," \
     "if every run hit its timeout, plus PaSTTeL"
if [ -n "${EXPECTED}" ]; then echo " Expected         : ${EXPECTED}"; fi
echo " Output           : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

if [ -n "${RUN[pulr]:-}" ]; then
    echo "################ ULR-Baseline (UAutomizer-linux) vs P-ULR ################"
    bash "${SCRIPT_DIR}/run_ulr_vs_pulr.sh" "${INPUT_ARGS[@]}" --output "${OUT_PULR}" \
        --ultimate-timeout "${ULTIMATE_TIMEOUT}"
    echo ""
fi

if [ -n "${RUN[upl]:-}" ]; then
    echo "################ Ultimate-LR vs Ultimate-PL (UAutomizer-PaSTTeL-linux, LassoRanker vs PaSTTeL backend) ################"
    bash "${SCRIPT_DIR}/run_ulr_vs_upl.sh" "${UPL_INPUT_ARGS[@]}" --output "${OUT_UPL}" \
        --timeout "${UPL_TIMEOUT}"
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
if [ -n "${RUN[pulr]:-}" ]; then
    check "ULR-Baseline vs P-ULR-Seq"  "${OUT_PULR}/results_P-ULR-Seq_z3.csv"
    check "ULR-Baseline vs ${PAR_LABEL}" "${OUT_PULR}/results_${PAR_LABEL}_z3.csv"
fi
if [ -n "${RUN[upl]:-}" ]; then
    check "Ultimate-LR vs Ultimate-PL (per program)" "${OUT_UPL}/results_ULR_vs_UPL.csv"
fi
# The paper's tables, each followed by the paths of its scatter plots and CSVs.
for part in pulr upl; do
    [ -n "${RUN[${part}]:-}" ] || continue
    d="${OUT_PULR}"; [ "${part}" = upl ] && d="${OUT_UPL}"
    if [ -s "${d}/summary_tables.log" ]; then echo ""; cat "${d}/summary_tables.log"; fi
done
echo "============================================================"
if [ "${status}" -ne 0 ]; then
    echo "${TITLE^^} FAILED: see the FAILED lines above and the logs next to each CSV." >&2
fi
exit "${status}"
