#!/bin/bash
# run_ulr_vs_upl.sh -- Compare Ultimate-LR (Ultimate with its LassoRanker backend) against
# Ultimate-PL (Ultimate with the PaSTTeL backend).
#
# Unlike run_ulr_vs_pulr.sh, which compares per *lasso trace* (Ultimate extracts
# traces, PaSTTeL replays them), this script compares per *program*:
# every input is analysed twice by a full Ultimate run, once with the stock
# LassoRanker rank-synthesis backend and once with the PaSTTeL backend for
# ranking functions, which falls back to LassoRanker when PaSTTeL does not conclude.
#
# Both runs use the SAME Ultimate release, tools/UAutomizer-PaSTTeL-linux, so the two
# configurations differ only in the rank-synthesis backend -- not in build date, bundled solvers
# or upstream revision. Ultimate-LR runs Ultimate with no settings file, exactly as
# run_ulr_vs_pulr.sh does to extract the lasso traces of ULR-Baseline vs P-ULR; Ultimate-PL adds
# tools/settings/BuchiAutomizerPasttel.epf.in, whose only lines select the
# PaSTTeL backend and configure it (binary, timeout, cores).
# Ultimate-LR is not ULR-Baseline: it verifies whole programs, while ULR-Baseline is LassoRanker
# timed on single lassos.
#
# --only ultimate-lr or --only ultimate-pl runs one side. Its logs (<program>.ulr.* or
# <program>.upl.*) join those already in <output>/logs, so the two sides may be run one after the
# other with the same --output: the second run prints the table.
#
# Usage:
#   bash scripts/run_ulr_vs_upl.sh [--input <dir|file>]...   (repeatable; default: benchmarks/smoke_test/full_programs_c_bpl)
#                                  [--only ultimate-lr|ultimate-pl] (default: both sides)
#                                  [--output <dir>]          (default: output/ulr_vs_upl)
#                                  [--timeout <sec>]         (default: 1000, per Ultimate run, as in the paper)
#                                  [--pasttel-timeout <sec>] (default: 20, per lasso inside Ultimate-PL; see common.sh)
#                                  [--pasttel-cpus <int>]    (default: 5, cores for PaSTTeL inside Ultimate-PL; see common.sh)
#
# Environment overrides: APP_DIR, PASTTEL_BIN, ULTIMATE_UPL.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

declare -a INPUTS=()
# ulr, upl: the two sides, as their log files are named.
declare -a CONFIGS=(ulr upl)
declare -A SIDE=([ulr]=Ultimate-LR [upl]=Ultimate-PL)
OUTPUT_DIR="${APP_DIR}/output/ulr_vs_upl"
TIMEOUT="${UPL_ULTIMATE_TIMEOUT_DEFAULT}"
PASTTEL_TIMEOUT="${UPL_PASTTEL_TIMEOUT_DEFAULT}"
PASTTEL_CPUS="${UPL_PASTTEL_CPUS_DEFAULT}"

usage() { sed -n "2,$(grep -n '^# Environment overrides' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)           INPUTS+=("$2");       shift 2 ;;
        --only)            case "$2" in
                               ultimate-lr) CONFIGS=(ulr) ;;
                               ultimate-pl) CONFIGS=(upl) ;;
                               *) die "--only takes ultimate-lr or ultimate-pl, not '$2'" ;;
                           esac;                 shift 2 ;;
        --output)          OUTPUT_DIR="$2";      shift 2 ;;
        --timeout)         TIMEOUT="$2";         shift 2 ;;
        --pasttel-timeout) PASTTEL_TIMEOUT="$2"; shift 2 ;;
        --pasttel-cpus)    PASTTEL_CPUS="$2";    shift 2 ;;
        -h|--help)         usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

PARSER="${PASTTEL_HOME}/scripts/benchmark_ulr_vs_upl.py"
EPF_TEMPLATE="${SETTINGS_DIR}/BuchiAutomizerPasttel.epf.in"

# -- Sanity checks ------------------------------------------------------------
require_file "${PARSER}"       "parser"
require_file "${EPF_TEMPLATE}" "PaSTTeL settings template"
require_file "${PASTTEL_BIN}"  "pasttel binary"
[ -x "${PASTTEL_BIN}" ] || die "${PASTTEL_BIN} is not executable (run 'make -j' in ${PASTTEL_HOME})"
require_dir  "${TOOLCHAIN_DIR}" "Ultimate toolchains"

require_ultimate "${ULTIMATE_UPL}" "Ultimate-LR and Ultimate-PL"
# A release without the PaSTTeL classes accepts the PASTTEL setting and
# quietly ignores it, producing an Ultimate-PL column identical to Ultimate-LR. Refuse to run
# rather than emit a comparison that looks plausible and means nothing.
ultimate_has_pasttel "${ULTIMATE_UPL}" \
    || die "${ULTIMATE_UPL} has no PaSTTeL backend: its LassoRanker library
       (plugins/de.uni_freiburg.informatik.ultimate.lib.lassoranker_*.jar) holds no PasttelExecutor.
       It looks like a stock upstream release. Rebuild it from the ultimate/ submodule."

# -- Collect inputs (absolute: run_config runs Ultimate from inside its release) ---
[ ${#INPUTS[@]} -gt 0 ] || INPUTS=("${APP_DIR}/benchmarks/smoke_test/full_programs_c_bpl")
require_inputs "${INPUTS[@]}"
declare -a PROGRAMS=()
while IFS= read -r -d '' f; do PROGRAMS+=("$f"); done < <(collect_programs "${INPUTS[@]}")
[ ${#PROGRAMS[@]} -gt 0 ] || die "no .c/.bpl program found in: ${INPUTS[*]}"

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"
LOG_DIR="${OUTPUT_DIR}/logs"
mkdir -p "${LOG_DIR}"

# -- Materialise the Ultimate-PL settings from the template ---------------------------
EPF_UPL="${OUTPUT_DIR}/BuchiAutomizerPasttel.epf"
sed -e "s|@PASTTEL_BIN@|${PASTTEL_BIN}|g" \
    -e "s|@PASTTEL_TIMEOUT@|${PASTTEL_TIMEOUT}|g" \
    -e "s|@PASTTEL_CPUS@|${PASTTEL_CPUS}|g" \
    "${EPF_TEMPLATE}" > "${EPF_UPL}"

# Only @NAME@ counts: a .epf legitimately contains lines such as '@UltimateCore=0.0.1'.
LEFTOVERS=$(grep -vE '^[[:space:]]*#' "${EPF_UPL}" | grep -cE '@[A-Z_]+@' || true)
[ "${LEFTOVERS}" -eq 0 ] || die "unsubstituted placeholder left in ${EPF_UPL}"

CSV="${OUTPUT_DIR}/results_ULR_vs_UPL.csv"

echo "============================================================"
echo " Ultimate-LR vs Ultimate-PL -- per-program comparison"
echo "============================================================"
echo " Programs        : ${#PROGRAMS[@]} (from ${INPUTS[*]})"
echo " Release         : ${ULTIMATE_UPL}"
echo "                   (both sides; only the rank-synthesis backend differs)"
echo " pasttel binary  : ${PASTTEL_BIN}"
echo " z3 (Ultimate)   : $(ultimate_z3 "${ULTIMATE_UPL}")"
echo " Ultimate timeout: ${TIMEOUT}s per run"
echo " Sides           : $(for c in "${CONFIGS[@]}"; do printf '%s ' "${SIDE[$c]}"; done)"
echo " Settings        : Ultimate-LR none (Ultimate's defaults), Ultimate-PL ${EPF_UPL}"
echo " PaSTTeL         : ${PASTTEL_TIMEOUT}s per lasso, ${PASTTEL_CPUS} cpus"
echo " Output          : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

# run_config <settings> <program> <log>
# Runs Ultimate once and prints the wall-clock milliseconds on stdout. An empty
# <settings> runs it with no settings file, i.e. with Ultimate's defaults.
# Ultimate must run from its own directory: it resolves z3/cvc4/mathsat relatively.
run_config() {
    local settings="$1" prog="$2" log="$3"
    local tc start end status=0 ws
    local -a settings_args=()
    [ -z "${settings}" ] || settings_args=(-s "${settings}")
    tc="$(toolchain_for "${prog}")"
    ws="$(new_workspace)"
    start=$(now_ms)
    ( cd "${ULTIMATE_UPL}" && timeout "${TIMEOUT}" ./Ultimate -data "${ws}" \
        -tc "${tc}" "${settings_args[@]}" -i "${prog}" ) > "${log}" 2>&1 || status=$?
    end=$(now_ms)
    rm -rf "${ws}"
    # Sidecar <name>.<cfg>.exit: timeout(1) exits with 124 when it had to stop Ultimate, which is how
    # the parser tells a run that hit the time limit (TIMEOUT) from one that ended early without a
    # verdict (UNKNOWN).
    echo "${status}" > "${log%.log}.exit"
    echo "$((end - start))"
}

# Guard evaluated once, on the first program analysed by Ultimate-PL: if PaSTTeL cannot
# even be launched, every later row would silently be a LassoRanker run.
upl_preflight_done=false
upl_preflight() {
    local log="$1"
    "${upl_preflight_done}" && return 0
    upl_preflight_done=true
    if grep -q 'PaSTTeL invocation failed' "${log}"; then
        echo "" >&2
        echo "ERROR: Ultimate could not launch PaSTTeL, so Ultimate-PL silently degraded to Ultimate-LR." >&2
        grep -m1 'PaSTTeL invocation failed' "${log}" >&2
        echo "       Check that ${PASTTEL_BIN} exists and is executable." >&2
        exit 1
    fi
}

# Guard evaluated once per side, on its first run that analyses a lasso: both sides must do so with
# the settings of ULR-Baseline in ULR-Baseline vs P-ULR -- LassoRanker's partitioning off, linear rank and
# GNTA synthesis -- which only a release built from the current ultimate/ submodule has by default.
# An older build runs with partitioning on and nonlinear synthesis, silently: every row would then
# compare against another baseline than the paper's.
declare -A settings_checked=()
settings_preflight() {
    local cfg="$1" log="$2" found wrong
    [ -z "${settings_checked[${cfg}]:-}" ] || return 0
    found=$(grep -oE 'Enable LassoPartitioneer: (true|false)|(Termination|Nontermination) analysis: (NONLINEAR|LINEAR_WITH_GUESSES|LINEAR|DISABLED)' \
            "${log}" | sort -u || true)
    [ -n "${found}" ] || return 0    # no lasso analysed in this run: check the next one
    settings_checked[${cfg}]=1
    wrong=$(printf '%s\n' "${found}" | grep -vE ': (false|LINEAR)$' || true)
    if [ -n "${wrong}" ]; then
        echo "" >&2
        echo "ERROR: ${SIDE[${cfg}]} analysed its lassos with settings other than those of ULR-Baseline:" >&2
        printf '%s\n' "${wrong}" | sed 's/^/         /' >&2
        echo "       expected: Enable LassoPartitioneer: false, (Non)termination analysis: LINEAR." >&2
        echo "       Rebuild the release from the ultimate/ submodule, whose defaults are these." >&2
        echo "       Log: ${log}" >&2
        exit 1
    fi
}

n=0
for prog in "${PROGRAMS[@]}"; do
    n=$((n + 1))
    name="$(basename "${prog}")"
    toolchain_for "${prog}" >/dev/null || { echo "  skip (unsupported extension): ${name}"; continue; }
    printf '[%d/%d] %s\n' "${n}" "${#PROGRAMS[@]}" "${name}"

    for cfg in "${CONFIGS[@]}"; do
        settings=""; [ "${cfg}" = upl ] && settings="${EPF_UPL}"
        log="${LOG_DIR}/${name}.${cfg}.log"
        wall=$(run_config "${settings}" "${prog}" "${log}")
        [ "${cfg}" = upl ] && upl_preflight "${log}"
        settings_preflight "${cfg}" "${log}"
        # Sidecar: wall clock stays the script's own measurement, never log-derived.
        echo "${wall}" > "${LOG_DIR}/${name}.${cfg}.wall_ms"
        # The verdict as the CSV will have it: proved terminating, proved nonterminating, UNKNOWN
        # (Ultimate gave up, or stopped early without a verdict) or TIMEOUT.
        verdict=$(python3 "${PARSER}" --verdict "${log}" --timeout "${TIMEOUT}" 2>/dev/null || echo "?")
        printf '        %-11s wall %8s ms  %s\n' "${SIDE[${cfg}]}" "${wall}" "${verdict}"
    done
done

# -- The paper's table and its scatter plot (wall clock, the time a user of Ultimate waits) ---
# benchmark_ulr_vs_upl.py writes the CSV and the scatter plot next to it, then prints the table and
# the paths of both, and keeps that same text in summary_tables.log.
SUMMARY="${OUTPUT_DIR}/summary_tables.log"
# Name the axes after the settings actually used, so a figure read on its own
# still says which backend each side is -- both runs are "Ultimate" otherwise.
ULR_LABEL="Ultimate-LR — LassoRanker (default settings)"
UPL_LABEL="Ultimate-PL — PaSTTeL ($(basename "${EPF_UPL}"))"
echo ""
python3 "${PARSER}" --log-dir "${LOG_DIR}" --output "${CSV}" --timeout "${TIMEOUT}" --col wall --log-scale \
    --ulr-label "${ULR_LABEL}" --upl-label "${UPL_LABEL}" --summary-file "${SUMMARY}"
