#!/bin/bash
# run_ultimate_only.sh -- Extract lasso traces with Ultimate Buchi Automizer, then classify them.
#
# Every .c/.bpl program is analysed by one Ultimate release with its default settings (no .epf);
# the release dumps one lasso_trace_<N>.txt per CEGAR iteration, with LassoRanker's timings and
# verdicts (the ULR-Baseline). The traces are then sorted by variable category (split_specific.sh):
#   <output>/<CATEGORY>/lasso_traces_<program>/lasso_trace_<N>.txt
# with CATEGORY in ALL_INT_VARS, BOOLEAN_OP, REAL_VARS, ARRAY_OP (replayed by PaSTTeL), or
# UNKNOWN_LOOP, FUNCT_SIGNATURE, UNDEF_TYPE, SI_ARRAYS, OTHERS (Ultimate only).
#
# Usage:
#   bash scripts/run_ultimate_only.sh [--input <dir|file>]...   (repeatable; a bare path works too)
#                                     [--ultimate-home <dir>]   (default: tools/UAutomizer-linux)
#                                     [--output <dir>]          (default: output/ultimate/lasso_traces)
#                                     [--timeout <sec>]         (default: 3000, per program, as in the paper)
#                                     [--toolchain-dir <dir>]   (default: tools/toolchains)
#
# Examples:
#   bash scripts/run_ultimate_only.sh benchmarks/smoke_test/full_programs_c_bpl
#   bash scripts/run_ultimate_only.sh --input benchmarks/C --input benchmarks/BPL --timeout 300
#   bash scripts/run_ultimate_only.sh --ultimate-home tools/UAutomizer-PaSTTeL-linux benchmarks/smoke_test/full_programs_c_bpl
#
# Environment overrides: APP_DIR, TOOLCHAIN_DIR, ULTIMATE_ULR.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

declare -a INPUTS=()
RELEASE="${ULTIMATE_ULR}"
OUTPUT_DIR="${APP_DIR}/output/ultimate/lasso_traces"
TIMEOUT="${PULR_ULTIMATE_TIMEOUT_DEFAULT}"

usage() { sed -n "2,$(grep -n '^# Environment overrides' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --input)         INPUTS+=("$2");     shift 2 ;;
        --ultimate-home) RELEASE="$2";       shift 2 ;;
        --output)        OUTPUT_DIR="$2";    shift 2 ;;
        --timeout)       TIMEOUT="$2";       shift 2 ;;
        --toolchain-dir) TOOLCHAIN_DIR="$2"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; usage; exit 1 ;;
        *)               INPUTS+=("$1");     shift ;;
    esac
done

[ ${#INPUTS[@]} -gt 0 ] || { usage; die "no input given"; }
require_inputs "${INPUTS[@]}"
require_ultimate "${RELEASE}" "$(basename "${RELEASE}")"
require_dir "${TOOLCHAIN_DIR}" "Ultimate toolchains"
SPLIT_SH="${PASTTEL_HOME}/scripts/split_specific.sh"
require_file "${SPLIT_SH}" "trace classifier"
RELEASE="$(realpath "${RELEASE}")"

declare -a PROGRAMS=()
while IFS= read -r -d '' f; do PROGRAMS+=("$f"); done < <(collect_programs "${INPUTS[@]}")
[ ${#PROGRAMS[@]} -gt 0 ] || die "no .c/.bpl program found in: ${INPUTS[*]}"

# Fresh output: traces of an earlier run would be classified together with the new ones.
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(realpath "${OUTPUT_DIR}")"

echo "============================================================"
echo " Ultimate Buchi Automizer -- lasso trace extraction"
echo "============================================================"
echo " Programs   : ${#PROGRAMS[@]} (from ${INPUTS[*]})"
echo " Release    : ${RELEASE}  (default settings)"
echo " z3         : $(ultimate_z3 "${RELEASE}")"
echo " Toolchains : ${TOOLCHAIN_DIR}"
echo " Timeout    : ${TIMEOUT}s per program"
echo " Output     : ${OUTPUT_DIR}"
echo "============================================================"
echo ""

# Ultimate writes lasso_traces/ into its working directory, i.e. the release. A leftover from an
# interrupted run would be merged into the next program's traces, so move it out of the way first.
if [ -e "${RELEASE}/lasso_traces" ]; then
    echo "  Warning: moving a leftover ${RELEASE}/lasso_traces to ${OUTPUT_DIR}/orphan_lasso_traces"
    mv "${RELEASE}/lasso_traces" "${OUTPUT_DIR}/orphan_lasso_traces"
fi

n=0
with_traces=0
for prog in "${PROGRAMS[@]}"; do
    n=$((n + 1))
    name="$(basename "${prog}")"
    tc="$(toolchain_for "${prog}")"
    printf '  [%d/%d] %s\n' "${n}" "${#PROGRAMS[@]}" "${name}"
    ( cd "${RELEASE}" && timeout "${TIMEOUT}" ./Ultimate -tc "${tc}" -i "${prog}" ) \
        > "${OUTPUT_DIR}/${name}.ultimate.log" 2>&1 || true
    if [ -d "${RELEASE}/lasso_traces" ]; then
        trace_dir="${OUTPUT_DIR}/lasso_traces_${name}"
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
echo "[Classification] split_specific.sh"
(cd "${OUTPUT_DIR}" && bash "${SPLIT_SH}")
echo ""
echo "  Lasso traces: ${OUTPUT_DIR}/<CATEGORY>/lasso_traces_<program>/"
echo "============================================================"
