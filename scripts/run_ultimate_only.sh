#!/bin/bash
# run_ultimate_only.sh — Run Ultimate Buchi Automizer on a directory of programs.
#
# Processes all .c and .bpl files in <src_dir>, generates lasso traces for each,
# then classifies them using split_specific.sh.
#
# Usage:
#   bash /app/scripts/run_ultimate_only.sh <src_dir>
#                                           [--timeout <sec>]    (default: 600)
#                                           [--output <dir>] (default: /app/output/ultimate/lasso_traces)

set -e

ULTIMATE_HOME="${ULTIMATE_HOME:-/app/ultimate}"
PASTTEL_HOME="${PASTTEL_HOME:-/app/pasttel}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-${ULTIMATE_HOME}/toolchains}"
APP_DIR="$(dirname "$SCRIPT_DIR")"

SRC_DIR=""
TIMEOUT=600
OUTPUT_DIR="${APP_DIR}/output/ultimate/lasso_traces"


usage() {
    echo "Usage: bash $0 <src_dir> [--timeout <sec>] [--output <dir>]"
    echo ""
    echo "Arguments:"
    echo "  <src_dir>              Directory containing .c and/or .bpl files (required)"
    echo ""
    echo "Options:"
    echo "  --timeout <sec>        Timeout per program in seconds  (default: 600)"
    echo "  --output  <dir>        Output directory for lasso traces (default: ${APP_DIR}/output/ultimate/lasso_traces)"
    echo "  --help                 Show this help message"
    echo ""
    echo "Examples:"
    echo "  bash $0 /app/benchmarks/smoke_test/"
    echo "  bash $0 /app/benchmarks/C/ --timeout 300 --output /app/output/my_traces/"
}

# Parse positional + keyword args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout)    TIMEOUT="$2";    shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --help|-h)    usage; exit 0 ;;
        -*) echo "Unknown option: $1"; echo ""; usage; exit 1 ;;
        *)  SRC_DIR="$1"; shift ;;
    esac
done

if [ -z "$SRC_DIR" ] || [ ! -d "$SRC_DIR" ]; then
    [ -n "$SRC_DIR" ] && echo "Error: '$SRC_DIR' is not a directory."
    echo ""
    usage
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

# Resolve to absolute paths so they remain valid after cd "${ULTIMATE_HOME}"
OUTPUT_DIR="$(cd "${OUTPUT_DIR}" && pwd)"
SRC_DIR="$(cd "${SRC_DIR}" && pwd)"

echo "============================================================"
echo " Ultimate Buchi Automizer — standalone run"
echo "============================================================"
echo " Source dir : ${SRC_DIR}"
echo " Output dir : ${OUTPUT_DIR}"
echo " Timeout    : ${TIMEOUT}s per program"
echo "============================================================"
echo " Delete old ${OUTPUT_DIR} directory..."
echo "============================================================"
echo ""

SPLIT_SH="${PASTTEL_HOME}/scripts/split_specific.sh"

run_ultimate() {
    local file="$1"
    local ext="${file##*.}"
    local filename
    filename="$(basename "$file")"
    local trace_dir="${OUTPUT_DIR}/lasso_traces_${filename}"
    
    if [ "$ext" = "c" ]; then
        TOOL_CHAIN="${TOOLCHAIN_DIR}/BuchiAutomizerC.xml"
    elif [ "$ext" = "bpl" ]; then
        TOOL_CHAIN="${TOOLCHAIN_DIR}/BuchiAutomizerBpl.xml"
    else
        return
    fi

    echo "  → ${filename}"
    # Ultimate must be run from its own directory so it locates config/ and data/.
    # OUTPUT_DIR and file are absolute paths, so the redirect works regardless of cwd.
    (cd "${ULTIMATE_HOME}" && timeout "${TIMEOUT}" ./Ultimate -tc "${TOOL_CHAIN}" -i "${file}" \
        > "${OUTPUT_DIR}/${filename}.log" 2>&1 || true)

    if [ -d "${ULTIMATE_HOME}/lasso_traces" ]; then
        mv "${ULTIMATE_HOME}/lasso_traces" "${trace_dir}"
        count=$(find "${trace_dir}" -name "lasso_trace_*.txt" 2>/dev/null | wc -l)
        echo "    ${count} trace(s) — ULR-Baseline results:"
        python3 "${PASTTEL_HOME}/scripts/print_trace_summary.py" "${trace_dir}" 2>/dev/null || true
    else
        echo "    Warning: no lasso_traces/ generated for ${filename}"
    fi
}

count_files=0
for f in "${SRC_DIR}"/*.c "${SRC_DIR}"/*.bpl; do
    [ -f "$f" ] || continue
    run_ultimate "$f"
    count_files=$((count_files + 1))
done

echo ""
echo "  Processed ${count_files} program(s)."

# Classify traces
if [ -f "${SPLIT_SH}" ]; then
    echo ""
    echo "[Classification] Running split_specific.sh on ${OUTPUT_DIR}..."
    (cd "${OUTPUT_DIR}" && bash "${SPLIT_SH}")
else
    echo "  Warning: ${SPLIT_SH} not found. Skipping classification."
fi

echo ""
echo "  Lasso traces are in: ${OUTPUT_DIR}"
echo "  Supported categories for PaSTTeL: ALL_INT_VARS, BOOLEAN_OP, REAL_VARS"
echo "============================================================"
