#!/bin/bash
# run_smoke_test.sh -- Smoke test of the PaSTTeL artifact: the paper's two comparisons, on the
# 10 programs of benchmarks/smoke_test/full_programs_c_bpl, with a short timeout (~3 minutes).
#
#   [1] ULR-Baseline (tools/UAutomizer-linux)                           vs P-ULR-Seq and P-ULR-Par<N>
#   [2] Ultimate-LR (tools/UAutomizer-PaSTTeL-linux, LassoRanker backend) vs Ultimate-PL (same release,
#       PaSTTeL backend)
#
# [1] compares per lasso trace, [2] per program. Both run Ultimate with its default settings,
# except Ultimate-PL in [2]: its settings file only switches the rank-synthesis backend to PaSTTeL
# (tools/settings/BuchiAutomizerPasttel.epf.in). Only Z3 is used.
# This is scripts/run_full_evaluation.sh on these programs, with every Ultimate run capped at 120 s.
#
# Usage:
#   bash scripts/run_smoke_test.sh [--ultimate-timeout <sec>]  (default: 120, per Ultimate run; raise it
#                                                               on a slow or emulated machine)
#                                  [--output <dir>]            (default: output/smoke)
#
# Exits non-zero if either comparison produced no result, so that a Docker build running
# this script fails instead of shipping an image that silently does nothing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

ULTIMATE_TIMEOUT=120
OUTPUT_DIR="${APP_DIR}/output/smoke"

usage() { sed -n "2,$(grep -n '^# this script fails' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ultimate-timeout) ULTIMATE_TIMEOUT="$2"; shift 2 ;;
        --output)           OUTPUT_DIR="$2";       shift 2 ;;
        -h|--help)          usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

EVALUATION_TITLE="smoke test" exec bash "${SCRIPT_DIR}/run_full_evaluation.sh" \
    --input "${APP_DIR}/benchmarks/smoke_test/full_programs_c_bpl" \
    --output "${OUTPUT_DIR}" \
    --ultimate-timeout "${ULTIMATE_TIMEOUT}" \
    --upl-timeout "${ULTIMATE_TIMEOUT}"
