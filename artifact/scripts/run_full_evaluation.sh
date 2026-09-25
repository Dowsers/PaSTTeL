#!/bin/bash
# run_full_evaluation.sh -- Full P-ULR vs ULR-Baseline comparison pipeline.
#
# Pipeline:
#   1. Run Ultimate -tc BuchiAutomizer{C,Bpl}.xml on benchmarks/C/ and benchmarks/BPL/
#       Traces saved as: lasso_traces_<prog>.c/  or  lasso_traces_<prog>.bpl/
#   2. Classify generated lasso traces (split_specific.sh)
#   3a. Run PaSTTeL sequentially  (P-ULR-Seq,  --cpus 1, Z3)  --> results_P-ULR-Seq_z3.csv
#   3b. Run PaSTTeL in parallel   (P-ULR-Par4, --cpus 4, Z3)  --> results_P-ULR-Par4_z3.csv
#   3c. Run PaSTTeL in parallel   (P-ULR-Par4, --cpus 4, CVC5)--> results_P-ULR-Par4_cvc5.csv
#   4. Generate scatter plots:
#      Fig.6 & Table.1  -- ULR-Baseline vs P-ULR-Seq  (Z3)  [paper]
#      Fig.6 & Table.1 -- ULR-Baseline vs P-ULR-Par4 (Z3)   [paper]
#      None            -- ULR-Baseline vs P-ULR-Par4 (CVC5) [not in the paper]
#      Fig.7           -- Z3 vs CVC5 on P-ULR-Par4          [paper]
#
# Usage:
#   bash /app/scripts/run_full_evaluation.sh  [--timeout <sec>]
#                                             [--cpus <int>]
#                                             [--output <dir>]
#
#   Environment variables (override defaults):
#     APP_DIR        base directory  (default: parent of this script)
#     PASTTEL_HOME   PaSTTeL install (default: ${APP_DIR}/pasttel)
#     ULTIMATE_HOME  Ultimate install(default: ${APP_DIR}/ultimate)
#
#   Default timeout: 600s.
#   Default output:  ${APP_DIR}/output/full

set -e

TIMEOUT=600
CPUS=4
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${APP_DIR:-$(dirname "$SCRIPT_DIR")}"
PASTTEL_HOME="${PASTTEL_HOME:-${APP_DIR}/pasttel}"
ULTIMATE_HOME="${ULTIMATE_HOME:-${APP_DIR}/ultimate}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-${ULTIMATE_HOME}/toolchains}"
BENCH_C="${APP_DIR}/benchmarks/C"
BENCH_BPL="${APP_DIR}/benchmarks/BPL"
OUTPUT_DIR="${APP_DIR}/output/full"
SUPPORTED_CLASSES="ALL_INT_VARS BOOLEAN_OP REAL_VARS"

function help(){
    echo "Usage:    bash $0 [--timeout <sec>]  (default: 600)
    	 	    	    [--cpus <int>]     (default: 4)
    		  	    [--output <dir>]   (default: ${APP_DIR}/output/full)
	                    "
}


while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout)    TIMEOUT="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2";      shift 2 ;;
	--cpus) CPUS="$2";         shift 2;;
        -h|--help) help; exit 1 ;;
        *) echo "Unknown option: $1";  help ; exit 1 ;;
    esac
done


CSV_SEQ_Z3="${OUTPUT_DIR}/results_P-ULR-Seq_z3.csv"
CSV_Par_Z3="${OUTPUT_DIR}/results_P-ULR-Par${CPUS}_z3.csv"
CSV_Par_CVC5="${OUTPUT_DIR}/results_P-ULR-Par${CPUS}_cvc5.csv"

echo "============================================================"
echo " PaSTTeL Artifact -- Full Evaluation"
echo "============================================================"
echo " Benchmark C   : ${BENCH_C}"
echo " Benchmark BPL : ${BENCH_BPL}"
echo " Output dir    : ${OUTPUT_DIR}"
echo " Timeout       : ${TIMEOUT}s"
echo " Configs       : P-ULR-Seq (cpus=1, Z3)"
echo "                 P-ULR-Par${CPUS} (cpus=${CPUS}, Z3)"
echo "                 P-ULR-Par${CPUS} (cpus=${CPUS}, CVC5)"
echo " Supported cats : ${SUPPORTED_CLASSES}"
echo "============================================================"
echo " Delete old ${OUTPUT_DIR} directory..."
echo "============================================================"
echo ""

rm -rf "${OUTPUT_DIR}"

mkdir -p "${OUTPUT_DIR}"
LASSO_OUT="${OUTPUT_DIR}/lasso_traces"
mkdir -p "${LASSO_OUT}"

# -- Step 1: Run Ultimate -tc BuchiAutomizer toolchain -----------------------
echo "[1/4] Step 1 -- Ultimate toolchain (BuchiAutomizer) on benchmarks/C/ and benchmarks/BPL/"
echo "      (each program: up to ${TIMEOUT}s)"
echo ""

run_ultimate_toolchain() {
    local file="$1"
    local ext="${file##*.}"
    local filename
    filename="$(basename "$file")"
    local trace_dir="${LASSO_OUT}/lasso_traces_${filename}"

    if [ "$ext" = "c" ]; then
        TOOL_CHAIN="${TOOLCHAIN_DIR}/BuchiAutomizerC.xml"
    elif [ "$ext" = "bpl" ]; then
        TOOL_CHAIN="${TOOLCHAIN_DIR}/BuchiAutomizerBpl.xml"
    else
        return
    fi

    echo "  --> ${filename}"
    (cd "${ULTIMATE_HOME}" && timeout "${TIMEOUT}" ./Ultimate -tc "${TOOL_CHAIN}" -i "${file}" \
        > "${LASSO_OUT}/${filename}.ultimate.log" 2>&1 || true)

    if [ -d "${ULTIMATE_HOME}/lasso_traces" ]; then
        mv -f "${ULTIMATE_HOME}/lasso_traces" "${trace_dir}"
        count=$(find "${trace_dir}" -name "lasso_trace_*.txt" 2>/dev/null | wc -l)
        echo "    ${count} trace(s) -- ULR-Baseline results:"
        python3 "${PASTTEL_HOME}/scripts/print_trace_summary.py" "${trace_dir}" 2>/dev/null || true
    else
        echo "    Warning: no lasso_traces/ generated for ${filename}"
    fi
}

count_progs=0
#for bench_dir in "${BENCH_C}" "${BENCH_BPL}"; do
for bench_dir in "${APP_DIR}/benchmarks/limited_benchmark_vmcai27"; do
    [ -d "${bench_dir}" ] || continue
    for f in "${bench_dir}"/*.c "${bench_dir}"/*.bpl; do
        [ -f "$f" ] || continue
        run_ultimate_toolchain "$f"
        count_progs=$((count_progs + 1))
    done
done
echo ""
echo "  Step 1 done: ${count_progs} program(s) processed."

# -- Step 2: Classify traces with split_specific.sh ---------------------------
echo ""
echo "[2/4] Classifying lasso traces..."
echo ""

SPLIT_SH="${PASTTEL_HOME}/scripts/split_specific.sh"
if [ ! -f "${SPLIT_SH}" ]; then
    echo "ERROR: ${SPLIT_SH} not found."
    exit 1
fi

(cd "${LASSO_OUT}" && bash "${SPLIT_SH}")

# -- Step 3: Run PaSTTeL -- three configurations ------------------------------
echo ""
echo "[3/4] Running PaSTTeL on supported categories (ALL_INT_VARS, BOOLEAN_OP, REAL_VARS)..."
echo ""

run_pasttel_config() {
    local cpus="$1"
    local csv_out="$2"
    local label="$3"
    local solver="$4"

    echo "  +-----------------------------------------------------+"
    echo "  |  ${label} -- Solver: $(printf '%-33s' "${solver^^}")|"
    echo "  +-----------------------------------------------------+"
    : > "${csv_out}"

    for cls in ${SUPPORTED_CLASSES}; do
        local cls_dir="${LASSO_OUT}/${cls}"
        [ -d "${cls_dir}" ] || continue
        echo "  Category: ${cls}"
        for trace_dir in "${cls_dir}"/lasso_traces_*; do
            [ -d "${trace_dir}" ] || continue
            local prog_name
            prog_name="$(basename "${trace_dir#${LASSO_OUT}/}")"
            local prog_log="${LASSO_OUT}/${prog_name}.pasttel-${label}-${solver}.log"
            local n_traces
            n_traces=$(find "${trace_dir}" -maxdepth 1 -name "lasso_trace_*.txt" | wc -l)
            echo "  --> ${prog_name}"
            echo "    ${n_traces} trace(s) -- P-ULR results:"
            python3 "${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py" \
                --input-dir "${trace_dir}" \
                --pasttel-bin "${PASTTEL_HOME}/bin/pasttel" \
                --output "${csv_out}" \
                --check lasso \
                --cpus "${cpus}" \
                --solver "${solver}" \
                --timeout "${TIMEOUT}" \
                --strat both \
                --parse normal \
                > "${prog_log}" 2>&1 || true
            grep -E '^\*\*\* JSON name |  PaSTTeL result:|  PaSTTeL P-ULR:' "${prog_log}" \
            | awk '
                /^\*\*\* JSON name / { sub(/^\*\*\* JSON name[[:space:]]+/, ""); sub(/\.json$/, ".txt"); name=$0 }
                /  PaSTTeL result:/  { verdict=$NF }
                /  PaSTTeL P-ULR:/  { time=$(NF-1); printf "    %-55s -> %-20s|  %s ms\n", name, verdict, time }
            ' || true
        done
    done
    echo ""
}

run_pasttel_config 1 "${CSV_SEQ_Z3}"    "P-ULR-Seq"  z3
run_pasttel_config ${CPUS} "${CSV_Par_Z3}"   "P-ULR-Par${CPUS}" z3
run_pasttel_config ${CPUS} "${CSV_Par_CVC5}" "P-ULR-Par${CPUS}" cvc5

# -- Step 4: Generate scatter plots -------------------------------------------
echo ""
echo "[4/4] Generating scatter plots..."

PLOT_LOG="${OUTPUT_DIR}/summary_tables.log"
: > "${PLOT_LOG}"

HTML_FIG6A="${CSV_SEQ_Z3%.csv}_scatter.html"
HTML_FIG6A_PDF="${CSV_SEQ_Z3%.csv}_scatter.pdf"
HTML_FIG6B="${CSV_Par_Z3%.csv}_scatter.html"
HTML_FIG6B_PDF="${CSV_Par_Z3%.csv}_scatter.pdf"
HTML_FIG6C="${CSV_Par_CVC5%.csv}_scatter.html"
HTML_FIG6C_PDF="${CSV_Par_CVC5%.csv}_scatter.pdf"
HTML_FIG7="${OUTPUT_DIR}/full_z3_vs_cvc5.html"
HTML_FIG7_PDF="${OUTPUT_DIR}/full_z3_vs_cvc5.pdf"

# Fig.6 -- ULR-Baseline vs P-ULR-Seq (Z3)  [paper]
FIG6A_STATUS="skipped (no CSV)"
if [ -f "${CSV_SEQ_Z3}" ] && [ -s "${CSV_SEQ_Z3}" ]; then
    python3 "${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py" \
        --plot "${CSV_SEQ_Z3}" \
        --log \
        >> "${PLOT_LOG}" 2>&1 || true
    FIG6A_STATUS="ok"
fi

# Fig.6 -- ULR-Baseline vs P-ULR-Par (Z3)  [paper]
FIG6B_STATUS="skipped (no CSV)"
if [ -f "${CSV_Par_Z3}" ] && [ -s "${CSV_Par_Z3}" ]; then
    python3 "${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py" \
        --plot "${CSV_Par_Z3}" \
        --log \
        >> "${PLOT_LOG}" 2>&1 || true
    FIG6B_STATUS="ok"
fi

# Fig.6 -- ULR-Baseline vs P-ULR-Par (CVC5)  [not in the paper]
FIG6C_STATUS="skipped (no CSV)"
if [ -f "${CSV_Par_CVC5}" ] && [ -s "${CSV_Par_CVC5}" ]; then
    python3 "${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py" \
        --plot "${CSV_Par_CVC5}" \
        --log \
        >> "${PLOT_LOG}" 2>&1 || true
    FIG6C_STATUS="ok"
fi

# Fig.7 -- Z3 vs CVC5 on P-ULR-Par  [paper]
FIG7_STATUS="skipped (missing CSV)"
if [ -f "${CSV_Par_Z3}" ] && [ -s "${CSV_Par_Z3}" ] && \
   [ -f "${CSV_Par_CVC5}" ] && [ -s "${CSV_Par_CVC5}" ]; then
    python3 "${PASTTEL_HOME}/scripts/compare_csv.py" \
        --csv-x "${CSV_Par_CVC5}" \
        --csv-y "${CSV_Par_Z3}" \
        --col "P-ULR-Par" \
        --label-x "P-ULR-Par (CVC5)" \
        --label-y "P-ULR-Par (Z3)" \
        --timeout "${TIMEOUT}" \
        --output "${HTML_FIG7}" \
        --log \
        >> "${PLOT_LOG}" 2>&1 || true
    FIG7_STATUS="ok"
fi

echo ""
echo "============================================================"
echo " Full evaluation complete."
echo "------------------------------------------------------------"
echo " Data files:"
echo "   CSV (P-ULR-Seq,  Z3)  : ${CSV_SEQ_Z3}"
echo "   CSV (P-ULR-Par${CPUS}, Z3)  : ${CSV_Par_Z3}"
echo "   CSV (P-ULR-Par${CPUS}, CVC5): ${CSV_Par_CVC5}"
echo ""
echo " Comparison tables [paper]:"
echo "   ULR-Baseline vs P-ULR-Seq  (Z3):         1st table in ${PLOT_LOG}"
echo "   ULR-Baseline vs P-ULR-Par${CPUS} (Z3):         2nd table in ${PLOT_LOG}"
echo "   ULR-Baseline vs P-ULR-Par${CPUS} (CVC5):       3rd table in ${PLOT_LOG}"
echo "   P-ULR-Par${CPUS} (Z3) vs P-ULR-Par${CPUS} (CVC5):   4th table in ${PLOT_LOG}"
echo ""
echo " Fig.6a -- ULR-Baseline vs P-ULR-Seq (Z3)  [paper]:"
if [ "${FIG6A_STATUS}" = "ok" ]; then
    echo "   HTML : ${HTML_FIG6A}"
    [ -f "${HTML_FIG6A_PDF}" ] && echo "   PDF  : ${HTML_FIG6A_PDF}"
else
    echo "   ${FIG6A_STATUS}"
fi
echo ""
echo " Fig.6b -- ULR-Baseline vs P-ULR-Par${CPUS} (Z3)  [paper]:"
if [ "${FIG6B_STATUS}" = "ok" ]; then
    echo "   HTML : ${HTML_FIG6B}"
    [ -f "${HTML_FIG6B_PDF}" ] && echo "   PDF  : ${HTML_FIG6B_PDF}"
else
    echo "   ${FIG6B_STATUS}"
fi
echo ""
echo " Fig.6c -- ULR-Baseline vs P-ULR-Par${CPUS} (CVC5)  [not in the paper]:"
if [ "${FIG6C_STATUS}" = "ok" ]; then
    echo "   HTML : ${HTML_FIG6C}"
    [ -f "${HTML_FIG6C_PDF}" ] && echo "   PDF  : ${HTML_FIG6C_PDF}"
else
    echo "   ${FIG6C_STATUS}"
fi
echo ""
echo " Fig.7 -- Z3 vs CVC5 on P-ULR-Par${CPUS}  [paper]:"
if [ "${FIG7_STATUS}" = "ok" ]; then
    echo "   HTML : ${HTML_FIG7}"
    [ -f "${HTML_FIG7_PDF}" ] && echo "   PDF  : ${HTML_FIG7_PDF}"
else
    echo "   ${FIG7_STATUS}"
fi
echo ""
echo "  Pre-computed paper results: ${APP_DIR}/logs/"
echo "============================================================"
