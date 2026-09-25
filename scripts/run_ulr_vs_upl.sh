#!/bin/bash
# run_ulr_vs_upl.sh -- Compare Ultimate LassoRanker (ULR) against Ultimate PaSTTeL (UPL).
#
# Unlike run_full_evaluation.sh, which compares per *lasso trace* (Ultimate
# extracts traces, PaSTTeL replays them), this script compares per *program*:
# every input is analysed twice by a full Ultimate run, once with the stock
# LassoRanker rank-synthesis backend and once with the PaSTTeL backend, which
# falls back to LassoRanker whenever PaSTTeL does not conclude.
#
# By default both runs use the SAME Ultimate release, so the two configurations
# differ only in the value of 'Rank synthesis backend' and the five PaSTTeL
# settings beside it -- not in build date, bundled solvers or upstream revision.
# Pass --ulr-home to run the baseline from a different release instead; that is a
# deliberately different experiment (comparing builds, e.g. the shuffled one),
# and the script says so when it happens.
#
# Usage:
#   bash scripts/run_ulr_vs_upl.sh [--input <dir|file>]     (default: benchmarks/smoke_test/full_programs_c_bpl)
#                                  [--output <dir>]         (default: output/ulr_vs_upl)
#                                  [--timeout <sec>]        (default: 600, per Ultimate run)
#                                  [--pasttel-timeout <sec>](default: 20, per lasso inside UPL)
#                                  [--pasttel-cpus <int>]   (default: 7)
#                                  [--repeat <int>]         (default: 1, median over N runs)
#                                  [--dump-pasttel-io]      (off by default: dumping biases the timings)
#                                  [--skip-ulr | --skip-upl]
#                                  [--ulr-home <dir>]       (default: same release as UPL)
#                                  [--upl-home <dir>]       (default: tools/UAutomizer-PaSTTeL-linux)
#                                  [--no-plot]
#
# Environment overrides: APP_DIR, PASTTEL_BIN, ULTIMATE_ULR, ULTIMATE_UPL.

set -euo pipefail

# Captured before common.sh, which fills in a default and would otherwise make
# "the user exported ULTIMATE_ULR" indistinguishable from "nobody said anything".
ULR_HOME_ENV="${ULTIMATE_ULR:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

INPUT="${APP_DIR}/benchmarks/smoke_test/full_programs_c_bpl"
OUTPUT_DIR="${APP_DIR}/output/ulr_vs_upl"
TIMEOUT=600
PASTTEL_TIMEOUT=20
PASTTEL_CPUS=7
REPEAT=1
DUMP_IO=false
SKIP_ULR=false
SKIP_UPL=false
PLOT=true
# Empty means "same release as UPL"; resolved once the options are parsed so that
# --upl-home also moves the baseline.
ULR_HOME_OVERRIDE="${ULR_HOME_ENV}"

usage() { sed -n "2,30p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)           INPUT="$2";           shift 2 ;;
        --output)          OUTPUT_DIR="$2";      shift 2 ;;
        --timeout)         TIMEOUT="$2";         shift 2 ;;
        --pasttel-timeout) PASTTEL_TIMEOUT="$2"; shift 2 ;;
        --pasttel-cpus)    PASTTEL_CPUS="$2";    shift 2 ;;
        --repeat)          REPEAT="$2";          shift 2 ;;
        --dump-pasttel-io) DUMP_IO=true;         shift ;;
        --skip-ulr)        SKIP_ULR=true;        shift ;;
        --skip-upl)        SKIP_UPL=true;        shift ;;
        --ulr-home)        ULR_HOME_OVERRIDE="$2"; shift 2 ;;
        --upl-home)        ULTIMATE_UPL="$2";    shift 2 ;;
        --no-plot)         PLOT=false;           shift ;;
        -h|--help)         usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [ -n "${ULR_HOME_OVERRIDE}" ]; then
    ULTIMATE_ULR="${ULR_HOME_OVERRIDE}"
    SAME_RELEASE=false
else
    ULTIMATE_ULR="${ULTIMATE_UPL}"
    SAME_RELEASE=true
fi

PARSER="${PASTTEL_HOME}/scripts/benchmark_ulr_vs_upl.py"
EPF_TEMPLATE="${SETTINGS_DIR}/BuchiAutomizerPasttel.epf.in"
EPF_ULR="${SETTINGS_DIR}/BuchiAutomizer.epf"

# -- Sanity checks ------------------------------------------------------------
require_file "${PARSER}"       "parser"
require_file "${EPF_TEMPLATE}" "PaSTTeL settings template"
require_file "${EPF_ULR}"      "ULR settings"
require_file "${PASTTEL_BIN}"  "pasttel binary"
[ -x "${PASTTEL_BIN}" ] || die "${PASTTEL_BIN} is not executable (run 'make -j' in ${PASTTEL_HOME})"
require_dir  "${TOOLCHAIN_DIR}" "Ultimate toolchains"

"${SKIP_ULR}" || require_ultimate "${ULTIMATE_ULR}" "ULR"
if ! "${SKIP_UPL}"; then
    require_ultimate "${ULTIMATE_UPL}" "UPL"
    # A release without the PaSTTeL classes accepts the PASTTEL setting and
    # quietly ignores it, producing a UPL column identical to ULR. Refuse to run
    # rather than emit a comparison that looks plausible and means nothing.
    ultimate_has_pasttel "${ULTIMATE_UPL}" \
        || die "${ULTIMATE_UPL} has no PaSTTeL backend in its BuchiAutomizer plugin.
       It looks like a stock upstream release. Rebuild it from the ultimate/ submodule."
fi

# -- Collect inputs -----------------------------------------------------------
# Everything is resolved to an absolute path here: run_config has to cd into the
# Ultimate release (it looks up z3/cvc4/mathsat relative to its own directory),
# so any relative path given on the command line would break there.
declare -a PROGRAMS=()
if [ -f "${INPUT}" ]; then
    PROGRAMS+=("$(realpath "${INPUT}")")
elif [ -d "${INPUT}" ]; then
    while IFS= read -r -d '' f; do PROGRAMS+=("$f"); done \
        < <(find "$(realpath "${INPUT}")" -type f \( -name '*.c' -o -name '*.bpl' \) -print0 | sort -z)
else
    die "--input '${INPUT}' is neither a file nor a directory"
fi
[ ${#PROGRAMS[@]} -gt 0 ] || die "no .c/.bpl program found under ${INPUT}"

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"
LOG_DIR="${OUTPUT_DIR}/logs"
mkdir -p "${LOG_DIR}"
DUMP_DIR="${OUTPUT_DIR}/pasttel_io"
"${DUMP_IO}" && mkdir -p "${DUMP_DIR}"

# -- Materialise the UPL settings from the template ---------------------------
EPF_UPL="${OUTPUT_DIR}/BuchiAutomizerPasttel.epf"
sed -e "s|@PASTTEL_BIN@|${PASTTEL_BIN}|g" \
    -e "s|@PASTTEL_TIMEOUT@|${PASTTEL_TIMEOUT}|g" \
    -e "s|@PASTTEL_CPUS@|${PASTTEL_CPUS}|g" \
    -e "s|@DUMP_ENABLED@|${DUMP_IO}|g" \
    -e "s|@DUMP_DIR@|${DUMP_DIR}|g" \
    "${EPF_TEMPLATE}" > "${EPF_UPL}"
# Only @NAME@ counts: a .epf legitimately contains lines such as '@UltimateCore=0.0.1'.
grep -vE '^[[:space:]]*#' "${EPF_UPL}" | grep -qE '@[A-Z_]+@' \
    && die "unsubstituted placeholder left in ${EPF_UPL}"

CSV="${OUTPUT_DIR}/results_ULR_vs_UPL.csv"

echo "============================================================"
echo " ULR vs UPL -- per-program comparison"
echo "============================================================"
echo " Programs        : ${#PROGRAMS[@]} (from ${INPUT})"
if "${SAME_RELEASE}"; then
echo " Release         : ${ULTIMATE_UPL}"
echo "                   (both sides; only the rank-synthesis backend differs)"
else
echo " ULR release     : ${ULTIMATE_ULR}"
echo " UPL release     : ${ULTIMATE_UPL}"
echo "                   NOTE: different builds -- this compares releases, not just backends."
fi
echo " pasttel binary  : ${PASTTEL_BIN}"
echo " Ultimate timeout: ${TIMEOUT}s per run"
echo " PaSTTeL         : ${PASTTEL_TIMEOUT}s per lasso, ${PASTTEL_CPUS} cpus"
echo " Repeats         : ${REPEAT} (median reported)"
echo " I/O dumping     : ${DUMP_IO}"
echo " Output          : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

# run_config <label> <ultimate_home> <settings> <program> <log>
# Runs Ultimate once and prints the wall-clock milliseconds on stdout.
# Ultimate must run from its own directory: it resolves z3/cvc4/mathsat relatively.
run_config() {
    local label="$1" home="$2" settings="$3" prog="$4" log="$5"
    local tc start end
    tc="$(toolchain_for "${prog}")"
    start=$(now_ms)
    ( cd "${home}" && timeout "${TIMEOUT}" ./Ultimate \
        -tc "${tc}" -s "${settings}" -i "${prog}" ) > "${log}" 2>&1 || true
    end=$(now_ms)
    echo "$((end - start))"
}

# Guard evaluated once, on the first program analysed by UPL: if PaSTTeL cannot
# even be launched, every later row would silently be a LassoRanker run.
upl_preflight_done=false
upl_preflight() {
    local log="$1"
    "${upl_preflight_done}" && return 0
    upl_preflight_done=true
    if grep -q 'PaSTTeL invocation failed' "${log}"; then
        echo "" >&2
        echo "ERROR: Ultimate could not launch PaSTTeL, so UPL silently degraded to ULR." >&2
        grep -m1 'PaSTTeL invocation failed' "${log}" >&2
        echo "       Check that ${PASTTEL_BIN} exists and is executable." >&2
        exit 1
    fi
}

n=0
for prog in "${PROGRAMS[@]}"; do
    n=$((n + 1))
    name="$(basename "${prog}")"
    toolchain_for "${prog}" >/dev/null || { echo "  skip (unsupported extension): ${name}"; continue; }
    printf '[%d/%d] %s\n' "${n}" "${#PROGRAMS[@]}" "${name}"

    for cfg in ulr upl; do
        [ "${cfg}" = ulr ] && "${SKIP_ULR}" && continue
        [ "${cfg}" = upl ] && "${SKIP_UPL}" && continue
        if [ "${cfg}" = ulr ]; then home="${ULTIMATE_ULR}"; settings="${EPF_ULR}"
        else                        home="${ULTIMATE_UPL}"; settings="${EPF_UPL}"; fi

        canonical="${LOG_DIR}/${name}.${cfg}.log"
        times=(); logs=()
        for ((r = 1; r <= REPEAT; r++)); do
            run_log="${canonical}"
            [ "${REPEAT}" -gt 1 ] && run_log="${LOG_DIR}/${name}.${cfg}.run${r}.log"
            times+=("$(run_config "${cfg}" "${home}" "${settings}" "${prog}" "${run_log}")")
            logs+=("${run_log}")
            [ "${cfg}" = upl ] && upl_preflight "${run_log}"
        done
        # Report the median repeat, and keep that same run's log as the canonical
        # one: the parser derives the plugin and lasso timings from it, so they
        # must describe the very run whose wall clock is reported beside them.
        median_idx=$(for i in "${!times[@]}"; do printf '%s %s\n' "${times[$i]}" "$i"; done \
                     | sort -n | awk -v n="${#times[@]}" 'NR == int((n + 1) / 2) { print $2 }')
        [ "${logs[$median_idx]}" = "${canonical}" ] || cp -f "${logs[$median_idx]}" "${canonical}"
        # Sidecar: wall clock stays the script's own measurement, never log-derived.
        echo "${times[$median_idx]}" > "${LOG_DIR}/${name}.${cfg}.wall_ms"
        printf '        %-4s wall %8s ms\n' "${cfg^^}" "${times[$median_idx]}"
    done
done

echo ""
echo "Parsing logs into ${CSV} ..."
SUMMARY="${OUTPUT_DIR}/summary_tables.log"
python3 "${PARSER}" --log-dir "${LOG_DIR}" --output "${CSV}" --timeout "${TIMEOUT}" \
    --summary --summary-file "${SUMMARY}"

if "${PLOT}"; then
    # Name the axes after the settings actually used, so a figure read on its own
    # still says which backend each side is -- both runs are "Ultimate" otherwise.
    ULR_LABEL="ULR — LassoRanker ($(basename "${EPF_ULR}"))"
    UPL_LABEL="UPL — PaSTTeL ($(basename "${EPF_UPL}"))"
    "${SAME_RELEASE}" || {
        ULR_LABEL="${ULR_LABEL%)} , $(basename "${ULTIMATE_ULR}"))"
        UPL_LABEL="${UPL_LABEL%)} , $(basename "${ULTIMATE_UPL}"))"
    }
    for col in wall plugin lassos; do
        python3 "${PARSER}" --plot "${CSV}" --col "${col}" \
            --output "${OUTPUT_DIR}/results_ULR_vs_UPL_${col}.html" --log-scale \
            --ulr-label "${ULR_LABEL}" --upl-label "${UPL_LABEL}" || true
    done
fi

echo ""
echo "============================================================"
echo " Done."
echo "   CSV      : ${CSV}"
echo "   Tables   : ${SUMMARY}"
echo "   Plots    : ${OUTPUT_DIR}/results_ULR_vs_UPL_{wall,plugin,lassos}.html"
echo "   Raw logs : ${LOG_DIR}"
echo "   Settings : ${EPF_UPL}"
echo "============================================================"
