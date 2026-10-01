#!/bin/bash
# run_ulr_vs_pulr.sh -- ULR-Baseline vs P-ULR, per lasso trace.
#
# Three steps, which --only selects:
#   ulr  tools/UAutomizer-linux analyses every .c/.bpl program with its default settings (no .epf) and
#        dumps one lasso_trace_<N>.txt per lasso it checks, with LassoRanker's verdict and timings: the
#        ULR-Baseline. The traces are then sorted by variable category (pasttel/scripts/split_specific.sh):
#          <output>/lasso_traces/<CATEGORY>/lasso_traces_<program>/lasso_trace_<N>.txt
#        PaSTTeL replays ALL_INT_VARS, BOOLEAN_OP, REAL_VARS and ARRAY_OP; UNKNOWN_LOOP,
#        FUNCT_SIGNATURE, UNDEF_TYPE, SI_ARRAYS and OTHERS are Ultimate only.
#   seq  PaSTTeL (Z3) replays every trace on 1 core: P-ULR-Seq.
#   par  PaSTTeL (Z3) replays every trace on --par-cpus cores: P-ULR-Par7.
# Whenever PaSTTeL ran, the script ends with the paper's table and one scatter plot per P-ULR
# configuration, over every results_P-ULR-*_z3.csv of the output directory.
#
# Without ulr, seq and par replay the traces of --lassos: by default those of an earlier run with the
# same --output, or ours, unpacked from logs/ULR_vs_PULR_logs.zip (README, section 3.1).
#
# Usage:
#   bash scripts/run_ulr_vs_pulr.sh [--input <dir|file>]...    (repeatable; default: benchmarks/smoke_test/full_programs_c_bpl)
#                                   [--only <steps>]           (comma-separated among ulr, seq, par; default: ulr,seq,par)
#                                   [--lassos <dir>]           (traces to replay without ulr; default: <output>/lasso_traces)
#                                   [--output <dir>]           (default: output/ulr_vs_pulr)
#                                   [--ultimate-timeout <sec>] (default: 3000, per Ultimate run, as in the paper)
#                                   [--pasttel-timeout <sec>]  (default: 600, per PaSTTeL run on one trace, as in the paper)
#                                   [--par-cpus <int>]         (default: 7, cores of P-ULR-Par)
#
# Examples:
#   bash scripts/run_ulr_vs_pulr.sh --input benchmarks/ulr_vs_pulr                 # [1] in full
#   bash scripts/run_ulr_vs_pulr.sh --only ulr --input benchmarks/ulr_vs_pulr      # the lasso traces only
#   bash scripts/run_ulr_vs_pulr.sh --only seq --lassos output/paper/ULR_vs_PULR_logs/lasso_traces
#
# Environment overrides: APP_DIR, PASTTEL_BIN, ULTIMATE_ULR (a release that dumps lasso traces, e.g.
# one built from ultimate-verifier/).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

declare -a INPUTS=()
STEPS="ulr,seq,par"
LASSO_DIR=""
OUTPUT_DIR="${APP_DIR}/output/ulr_vs_pulr"
ULTIMATE_TIMEOUT="${PULR_ULTIMATE_TIMEOUT_DEFAULT}"
PASTTEL_TIMEOUT="${PULR_PASTTEL_TIMEOUT_DEFAULT}"
PAR_CPUS="${PULR_PAR_CPUS_DEFAULT}"
# Categories PaSTTeL handles; the other classes split_specific.sh produces are Ultimate only.
SUPPORTED_CLASSES="ALL_INT_VARS BOOLEAN_OP REAL_VARS ARRAY_OP"

usage() { sed -n "2,$(grep -n '^# Environment overrides' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)            INPUTS+=("$2");        shift 2 ;;
        --only)             STEPS="$2";            shift 2 ;;
        --lassos)           LASSO_DIR="$2";        shift 2 ;;
        --output)           OUTPUT_DIR="$2";       shift 2 ;;
        --ultimate-timeout) ULTIMATE_TIMEOUT="$2"; shift 2 ;;
        --pasttel-timeout)  PASTTEL_TIMEOUT="$2";  shift 2 ;;
        --par-cpus)         PAR_CPUS="$2";         shift 2 ;;
        -h|--help)          usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

declare -A RUN=()
IFS=',' read -r -a _steps <<< "${STEPS}"
for step in "${_steps[@]}"; do
    case "${step}" in
        ulr|seq|par) RUN[${step}]=1 ;;
        *) die "unknown step '${step}' in --only (expected ulr, seq, par)" ;;
    esac
done
[ ${#RUN[@]} -gt 0 ] || die "--only selects no step"
[ "${PAR_CPUS}" -gt 1 ] || die "--par-cpus must be at least 2 (P-ULR-Seq is the 1-core run)"
SEQ_LABEL="P-ULR-Seq"
PAR_LABEL="P-ULR-Par${PAR_CPUS}"

RELEASE="$(realpath "${ULTIMATE_ULR}")"
RELEASE_NAME="$(basename "${RELEASE}")"
BENCH_PY="${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py"
SPLIT_SH="${PASTTEL_HOME}/scripts/split_specific.sh"

# -- Sanity checks, for the selected steps only --------------------------------------
if [ -n "${RUN[ulr]:-}" ]; then
    [ -z "${LASSO_DIR}" ] || die "--lassos replays existing traces: leave ulr out of --only"
    [ ${#INPUTS[@]} -gt 0 ] || INPUTS=("${APP_DIR}/benchmarks/smoke_test/full_programs_c_bpl")
    require_inputs "${INPUTS[@]}"
    require_ultimate "${RELEASE}" "${RELEASE_NAME}"
    require_dir "${TOOLCHAIN_DIR}" "Ultimate toolchains"
    require_file "${SPLIT_SH}" "trace classifier"
    declare -a PROGRAMS=()
    while IFS= read -r -d '' f; do PROGRAMS+=("$f"); done < <(collect_programs "${INPUTS[@]}")
    [ ${#PROGRAMS[@]} -gt 0 ] || die "no .c/.bpl program found in: ${INPUTS[*]}"
elif [ ${#INPUTS[@]} -gt 0 ]; then
    die "--input gives programs to step ulr: add ulr to --only, or give traces with --lassos"
fi
if [ -n "${RUN[seq]:-}${RUN[par]:-}" ]; then
    require_file "${PASTTEL_BIN}" "pasttel binary"
    [ -x "${PASTTEL_BIN}" ] || die "${PASTTEL_BIN} is not executable (run 'make -j' in ${PASTTEL_HOME})"
    require_file "${BENCH_PY}" "benchmark script"
fi

mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"
if [ -z "${LASSO_DIR}" ]; then
    LASSO_DIR="${OUTPUT_DIR}/lasso_traces"
elif [ ! -d "${LASSO_DIR}" ]; then
    die "--lassos: no such directory: ${LASSO_DIR}"
fi
PASTTEL_LOGS="${OUTPUT_DIR}/pasttel_logs"
SUMMARY="${OUTPUT_DIR}/summary_tables.log"

# Fresh results for the steps run: CSV rows are appended trace by trace, so stale rows would mix in.
# New traces invalidate every P-ULR result; a replay invalidates its own configuration's only.
if [ -n "${RUN[ulr]:-}" ]; then
    rm -rf "${LASSO_DIR}" "${PASTTEL_LOGS}"
    rm -f "${OUTPUT_DIR}"/results_P-ULR-*_z3.csv "${OUTPUT_DIR}"/results_P-ULR-*_z3_scatter.html "${SUMMARY}"
fi
for label in $([ -n "${RUN[seq]:-}" ] && echo "${SEQ_LABEL}") $([ -n "${RUN[par]:-}" ] && echo "${PAR_LABEL}"); do
    rm -f "${OUTPUT_DIR}/results_${label}_z3.csv" "${OUTPUT_DIR}/results_${label}_z3_scatter.html"
    rm -f "${PASTTEL_LOGS}"/*."${label}".log
done

echo "============================================================"
echo " ULR-Baseline vs P-ULR -- per lasso trace"
echo "============================================================"
echo " Steps            : ${STEPS}"
if [ -n "${RUN[ulr]:-}" ]; then
echo " Programs         : ${#PROGRAMS[@]} (from ${INPUTS[*]})"
echo " Ultimate release : ${RELEASE}  (default settings)"
echo " z3 (Ultimate)    : $(ultimate_z3 "${RELEASE}")"
echo " Ultimate timeout : ${ULTIMATE_TIMEOUT}s per program"
fi
echo " Lasso traces     : ${LASSO_DIR}"
if [ -n "${RUN[seq]:-}${RUN[par]:-}" ]; then
echo " PaSTTeL          : ${PASTTEL_BIN}, Z3, ${PASTTEL_TIMEOUT}s per trace"
echo " Configurations   : $([ -n "${RUN[seq]:-}" ] && echo "${SEQ_LABEL} (1 cpu) ")$([ -n "${RUN[par]:-}" ] && echo "${PAR_LABEL} (${PAR_CPUS} cpus)")"
echo " Categories       : ${SUPPORTED_CLASSES}"
fi
echo " Output           : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

# -- ulr: Ultimate extracts the lasso traces, which are then sorted by category -----------
if [ -n "${RUN[ulr]:-}" ]; then
    echo "[ulr] ${RELEASE_NAME} -- lasso trace extraction"
    mkdir -p "${LASSO_DIR}"
    # Ultimate writes lasso_traces/ into its working directory, i.e. the release. A leftover from an
    # interrupted run would be merged into the next program's traces, so move it out of the way first.
    if [ -e "${RELEASE}/lasso_traces" ]; then
        echo "  Warning: moving a leftover ${RELEASE}/lasso_traces to ${LASSO_DIR}/orphan_lasso_traces"
        mv "${RELEASE}/lasso_traces" "${LASSO_DIR}/orphan_lasso_traces"
    fi
    n=0
    with_traces=0
    for prog in "${PROGRAMS[@]}"; do
        n=$((n + 1))
        name="$(basename "${prog}")"
        tc="$(toolchain_for "${prog}")"
        printf '  [%d/%d] %s\n' "${n}" "${#PROGRAMS[@]}" "${name}"
        ws="$(new_workspace)"        # a fresh Eclipse workspace per run (common.sh)
        ( cd "${RELEASE}" && timeout "${ULTIMATE_TIMEOUT}" ./Ultimate -data "${ws}" -tc "${tc}" -i "${prog}" ) \
            > "${LASSO_DIR}/${name}.ultimate.log" 2>&1 || true
        rm -rf "${ws}"
        if [ -d "${RELEASE}/lasso_traces" ]; then
            trace_dir="${LASSO_DIR}/lasso_traces_${name}"
            mv "${RELEASE}/lasso_traces" "${trace_dir}"
            with_traces=$((with_traces + 1))
            # ULR-Baseline verdict of each trace, as dumped by Ultimate (RESULT section).
            while IFS= read -r t; do
                printf '        %-26s ULR-Baseline: %s\n' "$(basename "$t")" \
                    "$(sed -n 's/^Lasso termination:[[:space:]]*//p' "$t" | head -1)"
            done < <(find "${trace_dir}" -maxdepth 1 -name 'lasso_trace_*.txt' | sort -V)
        else
            echo "        no lasso trace (see ${name}.ultimate.log)"
        fi
    done
    echo ""
    echo "  ${with_traces}/${#PROGRAMS[@]} program(s) produced lasso traces."
    echo ""
    echo "[ulr] Classification (split_specific.sh)"
    (cd "${LASSO_DIR}" && bash "${SPLIT_SH}")
    echo ""
fi

# -- seq, par: PaSTTeL replays the traces ---------------------------------------------
# The trace directories to replay: those of the supported categories when the traces are sorted, as
# step ulr leaves them and as in our zip; every lasso_traces_<program>/ otherwise.
declare -a TRACE_DIRS=() TRACE_CLASSES=()
collect_trace_dirs() {
    local cls d sorted=false
    for cls in ${SUPPORTED_CLASSES}; do [ -d "${LASSO_DIR}/${cls}" ] && sorted=true; done
    if "${sorted}"; then
        for cls in ${SUPPORTED_CLASSES}; do
            for d in "${LASSO_DIR}/${cls}"/lasso_traces_*/; do
                [ -d "${d}" ] && TRACE_DIRS+=("${d%/}") && TRACE_CLASSES+=("${cls}")
            done
        done
    else
        for d in "${LASSO_DIR}"/lasso_traces_*/; do
            [ -d "${d}" ] && TRACE_DIRS+=("${d%/}") && TRACE_CLASSES+=("")
        done
    fi
    return 0
}

# run_pasttel <cpus> <label>  -- one CSV for all the traces, one row per trace.
run_pasttel() {
    local cpus="$1" label="$2" csv="${OUTPUT_DIR}/results_$2_z3.csv" i trace_dir cls prog_log
    echo ""
    echo "  -- ${label} (${cpus} cpu$([ "${cpus}" -gt 1 ] && echo s)) --"
    mkdir -p "${PASTTEL_LOGS}"
    : > "${csv}"
    for i in "${!TRACE_DIRS[@]}"; do
        trace_dir="${TRACE_DIRS[$i]}"; cls="${TRACE_CLASSES[$i]}"
        prog_log="${PASTTEL_LOGS}/$(basename "${trace_dir}")${cls:+.${cls}}.${label}.log"
        echo "  ${cls:+${cls}/}$(basename "${trace_dir}")"
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
}

if [ -n "${RUN[seq]:-}${RUN[par]:-}" ]; then
    collect_trace_dirs
    [ ${#TRACE_DIRS[@]} -gt 0 ] || die "no lasso_traces_<program>/ to replay in ${LASSO_DIR}"
    echo "[seq/par] PaSTTeL (Z3) replays the traces of ${#TRACE_DIRS[@]} program(s)"
    [ -z "${RUN[seq]:-}" ] || run_pasttel 1 "${SEQ_LABEL}"
    [ -z "${RUN[par]:-}" ] || run_pasttel "${PAR_CPUS}" "${PAR_LABEL}"

    # -- The paper's table and the scatter plots --------------------------------------
    # Over every P-ULR CSV of the output directory, so that seq and par may be run one after the other.
    # benchmark_ultimate_vs_pasttel.py --table prints the table, writes each CSV's scatter plot next
    # to it, and ends with the paths of the plots and CSVs.
    echo ""
    echo "Table and scatter plots"
    declare -a CSVS=()
    for csv in "${OUTPUT_DIR}/results_${SEQ_LABEL}_z3.csv" "${OUTPUT_DIR}"/results_P-ULR-Par*_z3.csv; do
        [ -e "${csv}" ] || continue
        if [ -s "${csv}" ]; then CSVS+=("${csv}"); else echo "  $(basename "${csv}"): empty"; fi
    done
    echo ""
    if [ ${#CSVS[@]} -gt 0 ]; then
        python3 "${BENCH_PY}" --table "${CSVS[@]}" --log --timeout "${PASTTEL_TIMEOUT}" \
            --baseline-name "${RELEASE_NAME}" | tee "${SUMMARY}" || true
    else
        echo "ULR-Baseline vs P-ULR: no result" | tee "${SUMMARY}"
    fi
else
    echo "  Lasso traces: ${LASSO_DIR}/<CATEGORY>/lasso_traces_<program>/"
    echo "  Replay them with: bash scripts/run_ulr_vs_pulr.sh --only seq,par --output ${OUTPUT_DIR}"
fi
