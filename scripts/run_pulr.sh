#!/bin/bash
# run_pulr.sh -- ULR-Baseline vs P-ULR on the lasso traces of an Ultimate release.
#
# The comparison is per *lasso trace*: Ultimate extracts the traces, PaSTTeL replays each one.
#   1-2. scripts/run_ultimate_only.sh: the Ultimate release analyses every program with its
#      default settings (no .epf) and dumps one lasso_trace_<N>.txt per CEGAR iteration, with
#      LassoRanker's own timings (ULR-Baseline); the traces are then classified by variable category.
#   3. PaSTTeL (Z3) replays every trace of the supported categories twice: sequentially (P-ULR-Seq,
#      1 cpu) and in parallel (P-ULR-Par<N>, N cpus).
#   4. One CSV, one scatter plot and one summary table per PaSTTeL configuration.
#
# The ULR-Baseline comes from the release's own dumps: tools/UAutomizer-linux by default, the one
# the paper uses.
#
# Usage:
#   bash scripts/run_pulr.sh [--ultimate-home <release dir>] (default: tools/UAutomizer-linux)
#                            [--input <dir|file>]...      (repeatable; default: benchmarks/smoke_test/full_programs_c_bpl)
#                            [--output <dir>]             (default: output/pulr_<release name>)
#                            [--ultimate-timeout <sec>]   (default: 3000, per Ultimate run, as in the paper)
#                            [--pasttel-timeout <sec>]    (default: 600, per PaSTTeL run on one trace, as in the paper)
#                            [--par-cpus <int>]           (default: 7, cores of the P-ULR-Par run)
#                            [--toolchain-dir <dir>]      (default: tools/toolchains)
#
# Environment overrides: APP_DIR, PASTTEL_BIN, TOOLCHAIN_DIR, ULTIMATE_ULR.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

RELEASE="${ULTIMATE_ULR}"
declare -a INPUTS=()
OUTPUT_DIR=""
ULTIMATE_TIMEOUT="${PULR_ULTIMATE_TIMEOUT_DEFAULT}"
PASTTEL_TIMEOUT="${PULR_PASTTEL_TIMEOUT_DEFAULT}"
PAR_CPUS=7
# Categories PaSTTeL handles; the other classes split_specific.sh produces (UNKNOWN_LOOP, OTHERS, ...)
# are Ultimate-only.
SUPPORTED_CLASSES="ALL_INT_VARS BOOLEAN_OP REAL_VARS ARRAY_OP"

usage() { sed -n "2,$(grep -n '^# Environment overrides' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ultimate-home)    RELEASE="$2";          shift 2 ;;
        --input)            INPUTS+=("$2");        shift 2 ;;
        --output)           OUTPUT_DIR="$2";       shift 2 ;;
        --ultimate-timeout) ULTIMATE_TIMEOUT="$2"; shift 2 ;;
        --pasttel-timeout)  PASTTEL_TIMEOUT="$2";  shift 2 ;;
        --par-cpus)         PAR_CPUS="$2";         shift 2 ;;
        --toolchain-dir)    TOOLCHAIN_DIR="$2";    shift 2 ;;
        -h|--help)          usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

[ ${#INPUTS[@]} -gt 0 ] || INPUTS=("${APP_DIR}/benchmarks/smoke_test/full_programs_c_bpl")
require_inputs "${INPUTS[@]}"
RELEASE="$(realpath "${RELEASE}")"
RELEASE_NAME="$(basename "${RELEASE}")"
OUTPUT_DIR="${OUTPUT_DIR:-${APP_DIR}/output/pulr_${RELEASE_NAME}}"

BENCH_PY="${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py"

# -- Sanity checks ------------------------------------------------------------
require_ultimate "${RELEASE}" "${RELEASE_NAME}"
require_file "${PASTTEL_BIN}" "pasttel binary"
[ -x "${PASTTEL_BIN}" ] || die "${PASTTEL_BIN} is not executable (run 'make -j' in ${PASTTEL_HOME})"
require_file "${BENCH_PY}" "benchmark script"

# Fresh output: CSV rows are appended program by program, so stale rows would mix in.
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"
LASSO_OUT="${OUTPUT_DIR}/lasso_traces"

echo "============================================================"
echo " ULR-Baseline vs P-ULR -- ${RELEASE_NAME}"
echo "============================================================"
echo " Inputs           : ${INPUTS[*]}"
echo " Ultimate release : ${RELEASE}  (default settings)"
echo " z3 (Ultimate)    : $(ultimate_z3 "${RELEASE}")"
echo " Ultimate timeout : ${ULTIMATE_TIMEOUT}s per program"
echo " PaSTTeL          : ${PASTTEL_BIN}, Z3, ${PASTTEL_TIMEOUT}s per trace"
echo " Configurations   : P-ULR-Seq (1 cpu), P-ULR-Par${PAR_CPUS} (${PAR_CPUS} cpus)"
echo " Categories       : ${SUPPORTED_CLASSES}"
echo " Output           : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

# -- Steps 1-2: Ultimate extracts the lasso traces, which are then classified -------
echo "[1-2/4] Ultimate (${RELEASE_NAME}) -- lasso trace extraction and classification"
declare -a INPUT_ARGS=()
for in in "${INPUTS[@]}"; do INPUT_ARGS+=(--input "${in}"); done
bash "${SCRIPT_DIR}/run_ultimate_only.sh" "${INPUT_ARGS[@]}" --ultimate-home "${RELEASE}" \
    --output "${LASSO_OUT}" --timeout "${ULTIMATE_TIMEOUT}" --toolchain-dir "${TOOLCHAIN_DIR}"

# -- Step 3: PaSTTeL replays the traces -----------------------------------------
# run_pasttel <cpus> <label>  -- one CSV for the whole benchmark, one row per trace.
run_pasttel() {
    local cpus="$1" label="$2" csv="${OUTPUT_DIR}/results_$2_z3.csv"
    echo ""
    echo "  -- ${label} (${cpus} cpu$([ "${cpus}" -gt 1 ] && echo s)) --"
    : > "${csv}"
    local cls trace_dir prog_log
    for cls in ${SUPPORTED_CLASSES}; do
        [ -d "${LASSO_OUT}/${cls}" ] || continue
        for trace_dir in "${LASSO_OUT}/${cls}"/lasso_traces_*; do
            [ -d "${trace_dir}" ] || continue
            prog_log="${LASSO_OUT}/$(basename "${trace_dir}").${cls}.pasttel-${label}.log"
            echo "  ${cls}/$(basename "${trace_dir}")"
            python3 "${BENCH_PY}" \
                --input-dir "${trace_dir}" --pasttel-bin "${PASTTEL_BIN}" --output "${csv}" \
                --check lasso --parse normal --strat both --solver z3 \
                --cpus "${cpus}" --timeout "${PASTTEL_TIMEOUT}" \
                > "${prog_log}" 2>&1 || true
            grep -E '^\*\*\* JSON name |  PaSTTeL result:|  PaSTTeL P-ULR:' "${prog_log}" | awk '
                /^\*\*\* JSON name / { sub(/^\*\*\* JSON name[[:space:]]+/, ""); sub(/\.json$/, ".txt"); name=$0 }
                /  PaSTTeL result:/  { verdict=$NF }
                /  PaSTTeL P-ULR:/   { printf "        %-26s P-ULR: %-16s %s ms\n", name, verdict, $(NF-1) }
            ' || true
        done
    done
}

echo ""
echo "[3/4] PaSTTeL replays the traces (Z3)"
run_pasttel 1 "P-ULR-Seq"
if [ "${PAR_CPUS}" -gt 1 ]; then
    run_pasttel "${PAR_CPUS}" "P-ULR-Par${PAR_CPUS}"
fi

# -- Step 4: plots and summary tables --------------------------------------------
echo ""
echo "[4/4] Scatter plots and summary tables"
SUMMARY="${OUTPUT_DIR}/summary_tables.log"
: > "${SUMMARY}"
declare -a LABELS=("P-ULR-Seq")
[ "${PAR_CPUS}" -gt 1 ] && LABELS+=("P-ULR-Par${PAR_CPUS}")
for label in "${LABELS[@]}"; do
    csv="${OUTPUT_DIR}/results_${label}_z3.csv"
    [ -s "${csv}" ] || { echo "  $(basename "${csv}"): empty, no plot"; continue; }
    {
        echo "=== $(basename "${csv}") -- ULR-Baseline from ${RELEASE_NAME} ==="
        python3 "${BENCH_PY}" --plot "${csv}" --log --timeout "${PASTTEL_TIMEOUT}" \
            --baseline-name "${RELEASE_NAME}" 2>&1 || true
        echo ""
    } >> "${SUMMARY}"
    echo "  $(basename "${csv%.csv}")_scatter.html"
done

# The same tables on the console, so that a run's output shows its results and not only paths.
echo ""
echo "  Summary tables -- ULR-Baseline (${RELEASE_NAME}) vs P-ULR:"
grep -v '^Scatter plot written to:' "${SUMMARY}" | sed 's/^/  /'

echo ""
echo "============================================================"
echo " Done -- ${RELEASE_NAME}"
echo "   CSV     : ${OUTPUT_DIR}/results_P-ULR-*_z3.csv"
echo "   Plots   : ${OUTPUT_DIR}/results_P-ULR-*_z3_scatter.html"
echo "   Tables  : ${SUMMARY}"
echo "   Traces  : ${LASSO_OUT}"
echo "============================================================"
