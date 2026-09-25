#!/bin/bash
# run_smoke_test.sh — Smoke test for the PaSTTeL artifact.
#
# Pipeline:
#   1. Run Ultimate Buchi Automizer (ULR-Baseline) on benchmarks/smoke_test/
#   2. Classify generated lasso traces (split_specific.sh)
#   3. Run PaSTTeL sequentially (--cpus 1) with Z3 then CVC5
#      on supported categories (ALL_INT_VARS, BOOLEAN_OP, REAL_VARS)
#   4. Generate scatter plots:
#      Fig.6 — ULR-Baseline vs P-ULR-Seq (Z3)   [reproduced from the paper]
#      Fig.7 — Z3 vs CVC5 on P-ULR-Seq          [reproduced from the paper]
#
# Usage:
#   bash /app/scripts/run_smoke_test.sh [--timeout <seconds>]
#                                       [--cpus <integer>]
#
#   Default PaSTTeL timeout: 120s, cpus: 1.
#   ULR-Baseline timeout: 120s.

set -e

TIMEOUT=120
PASTTEL_CPUS=1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$(dirname "$SCRIPT_DIR")"
PASTTEL_HOME="${PASTTEL_HOME:-/app/pasttel}"
ULTIMATE_HOME="${ULTIMATE_HOME:-/app/ultimate}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-${ULTIMATE_HOME}/toolchains}"
SMOKE_BENCH="${APP_DIR}/benchmarks/smoke_test/full_programs_c_bpl"
OUTPUT_DIR="${APP_DIR}/output/smoke"
SUPPORTED_CLASSES="ALL_INT_VARS BOOLEAN_OP REAL_VARS"


function help(){
	echo "Usage:    bash $0 [--cpus N]          (default: 1)
	                        [--timeout <sec>]   (default: 120)
	                        "
}


while [[ $# -gt 0 ]]; do
    case "$1" in
        --cpus)    PASTTEL_CPUS="$2";    shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        -h|--help) help; exit 1 ;;
        *) echo "Unknown option: $1";  help ; exit 1 ;;
    esac
done

# P-ULR label: Seq if cpus=1, Par{N} otherwise
if [ "${PASTTEL_CPUS}" -eq 1 ]; then
    PULR_LABEL="P-ULR-Seq"
else
    PULR_LABEL="P-ULR-Par${PASTTEL_CPUS}"
fi

CSV_Z3="${OUTPUT_DIR}/smoke_results_${PULR_LABEL}_z3.csv"
CSV_CVC5="${OUTPUT_DIR}/smoke_results_${PULR_LABEL}_cvc5.csv"


echo "============================================================"
echo " PaSTTeL Artifact — Smoke Test"
echo "============================================================"
echo " Benchmark dir  : ${SMOKE_BENCH}"
echo " Output dir     : ${OUTPUT_DIR}"
echo " PaSTTeL timeout: ${TIMEOUT}s $([ ${PASTTEL_CPUS} -eq 1 ] && echo '(sequential, --cpus 1)' || echo "(parallel, --cpus ${PASTTEL_CPUS})")"
echo " Solvers        : Z3 (Fig.6, paper) + CVC5 (Fig.7, paper)"
echo " Supported cats : ${SUPPORTED_CLASSES}"
echo "============================================================"
echo " Delete old ${OUTPUT_DIR} directory..."
echo "============================================================"
echo ""

rm -rf ${OUTPUT_DIR}

mkdir -p "${OUTPUT_DIR}"
LASSO_OUT="${OUTPUT_DIR}/lasso_traces"
mkdir -p "${LASSO_OUT}"

# ── Step 1: Run Ultimate Buchi Automizer (ULR-Baseline) ─────────────────────────────────────
echo "[1/4] Running Ultimate Buchi Automizer (ULR-Baseline) on smoke test programs..."
echo "      (each program: up to 120s)"
echo ""

run_ultimate() {
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

    echo "  → ${filename}"
    (cd "${ULTIMATE_HOME}" && timeout 120 ./Ultimate -tc "${TOOL_CHAIN}" -i "${file}" \
        > "${LASSO_OUT}/${filename}.ultimate.log" 2>&1 || true)

    if [ -d "${ULTIMATE_HOME}/lasso_traces" ]; then
        mv -f "${ULTIMATE_HOME}/lasso_traces" "${trace_dir}"
        count=$(find "${trace_dir}" -name "lasso_trace_*.txt" 2>/dev/null | wc -l)
        echo "    ${count} trace(s) — ULR-Baseline results:"
        python3 "${PASTTEL_HOME}/scripts/print_trace_summary.py" "${trace_dir}" 2>/dev/null || true
    else
        echo "    Warning: no lasso_traces/ generated for ${filename}"
    fi
}

for f in "${SMOKE_BENCH}"/*.c "${SMOKE_BENCH}"/*.bpl; do
    [ -f "$f" ] || continue
    run_ultimate "$f"
done

# ── Step 2: Classify traces with split_specific.sh ──────────────────────────
echo ""
echo "[2/4] Classifying lasso traces..."
echo ""

SPLIT_SH="${PASTTEL_HOME}/scripts/split_specific.sh"
if [ ! -f "${SPLIT_SH}" ]; then
    echo "ERROR: ${SPLIT_SH} not found."
    exit 1
fi

(cd "${LASSO_OUT}" && bash "${SPLIT_SH}")

# ── Step 3: Run PaSTTeL — Z3 then CVC5 ───────────────────────────────────────
echo ""
echo "[3/4] Running PaSTTeL on supported categories (ALL_INT_VARS, BOOLEAN_OP, REAL_VARS)..."
echo "      PaSTTeL runs $([ ${PASTTEL_CPUS} -eq 1 ] && echo 'sequentially (--cpus 1)' || echo "concurrently (--cpus ${PASTTEL_CPUS})")"
echo ""

run_pasttel_solver() {
    local solver="$1"
    local csv_out="$2"
    echo "  ┌─────────────────────────────────────────┐"
    echo "  │  Solver: $(printf '%-31s' "${solver^^}")│"
    echo "  └─────────────────────────────────────────┘"
    : > "${csv_out}"
    for cls in ${SUPPORTED_CLASSES}; do
        local cls_dir="${LASSO_OUT}/${cls}"
        [ -d "${cls_dir}" ] || continue
        echo "  Category: ${cls}"
        for trace_dir in "${cls_dir}"/lasso_traces_*; do
            [ -d "${trace_dir}" ] || continue
            local prog_name
            prog_name="$(basename "${trace_dir#${LASSO_OUT}/}")"
            local prog_log="${LASSO_OUT}/${prog_name}.pasttel-${solver}.log"
            local n_traces
            n_traces=$(find "${trace_dir}" -maxdepth 1 -name "lasso_trace_*.txt" | wc -l)
            echo "  → ${prog_name}"
            echo "    ${n_traces} trace(s) — P-ULR results:"
            python3 "${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py" \
                --input-dir "${trace_dir}" \
                --pasttel-bin "${PASTTEL_HOME}/bin/pasttel" \
                --output "${csv_out}" \
                --check lasso \
                --cpus "${PASTTEL_CPUS}" \
                --solver "${solver}" \
                --timeout "${TIMEOUT}" \
                --strat both \
                --parse normal \
                > "${prog_log}" 2>&1 || true
            grep -E '^\*\*\* JSON name |  PaSTTeL result:|  PaSTTeL P-ULR:' "${prog_log}" \
            | awk '
                /^\*\*\* JSON name / { sub(/^\*\*\* JSON name[[:space:]]+/, ""); sub(/\.json$/, ".txt"); name=$0 }
                /  PaSTTeL result:/  { verdict=$NF }
                /  PaSTTeL P-ULR:/  { time=$(NF-1); printf "    %-55s → %-20s|  %s ms\n", name, verdict, time }
            ' || true
        done
    done
    echo ""
}

run_pasttel_solver z3   "${CSV_Z3}"
run_pasttel_solver cvc5 "${CSV_CVC5}"

# ── Step 4: Generate scatter plots ────────────────────────────────────────────
echo ""
echo "[4/4] Generating scatter plots..."

PLOT_LOG="${OUTPUT_DIR}/summary_tables.log"
: > "${PLOT_LOG}"

# Fig.6 — ULR-Baseline vs P-ULR-Seq (Z3)  [paper]
HTML_FIG6="${CSV_Z3%.csv}_scatter.html"
HTML_FIG6_PDF="${CSV_Z3%.csv}_scatter.pdf"
FIG6_STATUS="skipped (no CSV)"
if [ -f "${CSV_Z3}" ] && [ -s "${CSV_Z3}" ]; then
    python3 "${PASTTEL_HOME}/scripts/benchmark_ultimate_vs_pasttel.py" \
        --plot "${CSV_Z3}" \
        --log \
        >> "${PLOT_LOG}" 2>&1 || true
    FIG6_STATUS="ok"
fi

# Fig.7 — Z3 vs CVC5 on P-ULR-Seq  [paper]
HTML_FIG7="${OUTPUT_DIR}/smoke_z3_vs_cvc5.html"
HTML_FIG7_PDF="${OUTPUT_DIR}/smoke_z3_vs_cvc5.pdf"
FIG7_STATUS="skipped (missing CSV)"
if [ -f "${CSV_Z3}" ] && [ -s "${CSV_Z3}" ] && \
   [ -f "${CSV_CVC5}" ] && [ -s "${CSV_CVC5}" ]; then
    python3 "${PASTTEL_HOME}/scripts/compare_csv.py" \
        --csv-x "${CSV_CVC5}" \
        --csv-y "${CSV_Z3}" \
        --col "${PULR_LABEL}" \
        --label-x "${PULR_LABEL} (CVC5)" \
        --label-y "${PULR_LABEL} (Z3)" \
        --timeout "${TIMEOUT}" \
        --output "${HTML_FIG7}" \
        --log \
        >> "${PLOT_LOG}" 2>&1 || true
    FIG7_STATUS="ok"
fi

echo "============================================================="
echo " Summary Tables"
echo "-------------------------------------------------------------"
cat "${PLOT_LOG}"
echo "============================================================="

echo ""
echo "============================================================"
echo " Smoke test complete."
echo "------------------------------------------------------------"
echo " Data files:"
echo "   CSV (Z3)   : ${CSV_Z3}"
echo "   CSV (CVC5) : ${CSV_CVC5}"
echo ""
echo " Comparison tables [paper]:"
echo "   ULR-Baseline vs ${PULR_LABEL}: First table ${PLOT_LOG}"
echo "   ${PULR_LABEL} (Z3) vs ${PULR_LABEL} (CVC5): Second table ${PLOT_LOG}"
echo ""
echo " Fig.6 — ULR-Baseline vs ${PULR_LABEL} (Z3)  [paper]:"
if [ "${FIG6_STATUS}" = "ok" ]; then
    echo "   HTML : ${HTML_FIG6}"
    [ -f "${HTML_FIG6_PDF}" ] && echo "   PDF  : ${HTML_FIG6_PDF}"
else
    echo "   ${FIG6_STATUS}"
fi
echo ""
echo " Fig.7 — Z3 vs CVC5 on ${PULR_LABEL}  [paper]:"
if [ "${FIG7_STATUS}" = "ok" ]; then
    echo "   HTML : ${HTML_FIG7}"
    [ -f "${HTML_FIG7_PDF}" ] && echo "   PDF  : ${HTML_FIG7_PDF}"
else
    echo "   ${FIG7_STATUS}"
fi
echo "============================================================"
