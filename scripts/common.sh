# common.sh -- shared path resolution and sanity checks for the evaluation scripts.
# Sourced, not executed.

# Repository root: the parent of scripts/.
COMMON_SH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${APP_DIR:-$(dirname "${COMMON_SH_DIR}")}"

PASTTEL_HOME="${PASTTEL_HOME:-${APP_DIR}/pasttel}"
PASTTEL_BIN="${PASTTEL_BIN:-${PASTTEL_HOME}/bin/pasttel}"
TOOLS_DIR="${TOOLS_DIR:-${APP_DIR}/tools}"
SETTINGS_DIR="${SETTINGS_DIR:-${TOOLS_DIR}/settings}"
# Upstream toolchains, copied out of the submodule so the scripts do not depend
# on its layout. They must name plugins.generator.icfgbuilder: the older
# rcfgbuilder still ships in the release but rejects structured Boogie
# statements ("Did not expect statement of type WhileStatement"), and the .epf
# tunes icfgbuilder, so an rcfgbuilder toolchain also drops those settings.
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-${TOOLS_DIR}/toolchains}"

# The three Ultimate releases the paper's experiments need. They are binary
# releases built from the ultimate/ submodule; see README for how to regenerate.
ULTIMATE_ULR="${ULTIMATE_ULR:-${TOOLS_DIR}/UAutomizer-linux}"
ULTIMATE_ULR_SHUFFLE="${ULTIMATE_ULR_SHUFFLE:-${TOOLS_DIR}/UAutomizer-linux-shuffler}"
ULTIMATE_UPL="${ULTIMATE_UPL:-${TOOLS_DIR}/UAutomizer-PaSTTeL-linux}"

# Defaults taken from the paper's experiments. Each is defined here only; the scripts read them, and
# their options override them for one run.
#   PULR_ULTIMATE_TIMEOUT_DEFAULT  Ultimate budget per program when extracting the lasso traces of the
#                                  ULR vs P-ULR comparison, fixed or shuffled order (run_pulr.sh,
#                                  run_ultimate_only.sh, run_full_evaluation.sh --ultimate-timeout)
#   PULR_PASTTEL_TIMEOUT_DEFAULT   PaSTTeL budget per lasso trace in ULR vs P-ULR (run_pulr.sh,
#                                  run_full_evaluation.sh --pasttel-timeout)
#   UPL_ULTIMATE_TIMEOUT_DEFAULT   Ultimate budget per program for each of the two runs of ULR vs UPL
#                                  (run_ulr_vs_upl.sh --timeout, run_full_evaluation.sh --upl-timeout)
#   UPL_PASTTEL_TIMEOUT_DEFAULT    PaSTTeL budget per lasso inside UPL, past which Ultimate falls back to
#                                  LassoRanker; substituted for @PASTTEL_TIMEOUT@ in
#                                  tools/settings/BuchiAutomizerPasttel.epf.in. 20 s, the preference's own
#                                  default in the fork: UPL runs PaSTTeL on 7 cores, and in P-ULR-Par7
#                                  every trace PaSTTeL solved on the paper's benchmark (4,749) was solved
#                                  within 20 s. (run_ulr_vs_upl.sh --pasttel-timeout,
#                                  run_full_evaluation.sh --upl-pasttel-timeout)
PULR_ULTIMATE_TIMEOUT_DEFAULT=3000
PULR_PASTTEL_TIMEOUT_DEFAULT=600
UPL_ULTIMATE_TIMEOUT_DEFAULT=1000
UPL_PASTTEL_TIMEOUT_DEFAULT=20

die() { echo "ERROR: $*" >&2; exit 1; }

# require_file <path> <what>
require_file() {
    [ -f "$1" ] || die "$2 not found: $1"
}

require_dir() {
    [ -d "$1" ] || die "$2 not found: $1"
}

# require_ultimate <dir> <label>
# An Ultimate release is usable only if its launcher is there and executable.
require_ultimate() {
    local dir="$1" label="$2"
    require_dir "${dir}" "Ultimate release (${label})"
    [ -x "${dir}/Ultimate" ] || die "${dir}/Ultimate is missing or not executable (${label})"
    require_java 21
}

# require_java <major>
# The Ultimate launcher starts whatever `java` is on PATH (Ultimate.ini sets no -vm). The releases are
# compiled for Java 21; an older JVM fails only once Ultimate starts, with an UnsupportedClassVersionError
# buried in every per-program log, so check up front.
require_java() {
    local need="$1" java_bin version major
    java_bin="$(command -v java || true)"
    [ -n "${java_bin}" ] || die "no 'java' on PATH; Ultimate needs Java ${need} or newer"
    version="$(java -version 2>&1 | sed -n 's/.*version "\([^"]*\)".*/\1/p' | head -1)"
    # "1.8.0_392" -> 8, "21.0.12" -> 21
    major="${version%%.*}"
    [ "${major}" = "1" ] && major="$(echo "${version}" | cut -d. -f2)"
    if ! [ "${major}" -ge "${need}" ] 2>/dev/null; then
        die "Ultimate needs Java ${need} or newer, but '${java_bin}' is Java ${version:-unknown}.
       Select a newer JVM first (e.g. 'jenv local ${need}', or put its bin/ first on PATH)."
    fi
}

# ultimate_has_pasttel <dir>
# True when the release's BuchiAutomizer plugin actually carries the PaSTTeL
# backend. Without this check a release built from upstream silently ignores the
# PASTTEL setting and falls back to LassoRanker, which would make a UPL-vs-ULR
# comparison measure ULR against itself.
ultimate_has_pasttel() {
    local dir="$1" jar hits
    # -print -quit rather than a pipe to head: callers run under 'set -o pipefail',
    # where head closing the pipe early makes find die of SIGPIPE and the whole
    # pipeline report failure even though it found the jar.
    jar=$(find "${dir}/plugins" -maxdepth 1 \
          -name 'de.uni_freiburg.informatik.ultimate.plugins.generator.buchiautomizer_*.jar' \
          -print -quit 2>/dev/null)
    [ -n "${jar}" ] || return 1
    # -Z1 lists entry names only. Plain 'unzip -l' also echoes the archive's own
    # path, which matches 'pasttel' for any release stored under this repository.
    # Counting rather than 'grep -q' for the same pipefail reason: -q exits on the
    # first match, SIGPIPEs unzip, and intermittently turns a hit into a miss.
    hits=$(unzip -Z1 "${jar}" 2>/dev/null | grep -ci 'pasttel' || true)
    [ "${hits}" -gt 0 ]
}

# ultimate_z3 <release_dir>
# Echoes "<path> (<version>)" for the z3 that Ultimate will actually launch from this release,
# following MonitoredProcess.findExecutableBinary: a z3 in the release directory (Ultimate runs with
# it as working directory) wins over PATH. The artifact's releases ship none, so every tool uses the
# single z3 on PATH; a z3 put back into a release would silently override it, hence this report.
ultimate_z3() {
    local dir="$1" bin
    if [ -x "${dir}/z3" ]; then
        bin="${dir}/z3"
    else
        bin="$(command -v z3 || true)"
    fi
    if [ -z "${bin}" ]; then
        echo "NOT FOUND (neither in ${dir} nor on PATH)"
        return 1
    fi
    echo "${bin} ($("${bin}" -version 2>/dev/null | head -1 | sed 's/^Z3 version //; s/ - .*//'))"
}

# toolchain_for <file>  -- echoes the toolchain XML matching the input's extension.
toolchain_for() {
    case "${1##*.}" in
        c)   echo "${TOOLCHAIN_DIR}/BuchiAutomizerC.xml" ;;
        bpl) echo "${TOOLCHAIN_DIR}/BuchiAutomizerBpl.xml" ;;
        *)   return 1 ;;
    esac
}

# require_inputs <input>...  -- dies unless every input is an existing file or directory.
# Call it directly, before collect_programs: collect_programs is meant to be read through a process
# substitution, where a die would only end the substituted shell and go unnoticed.
require_inputs() {
    local in
    [ $# -gt 0 ] || die "no input given"
    for in in "$@"; do
        [ -f "${in}" ] || [ -d "${in}" ] || die "input '${in}' is neither a file nor a directory"
    done
}

# collect_programs <input>...
# Prints, NUL-separated, sorted and without duplicates, the absolute path of every .c/.bpl program
# given directly or found under a given directory. Absolute because Ultimate is run from inside
# its release directory, where a relative path would no longer resolve.
collect_programs() {
    local in
    for in in "$@"; do
        if [ -f "${in}" ]; then
            printf '%s\0' "$(realpath "${in}")"
        else
            find "$(realpath "${in}")" -type f \( -name '*.c' -o -name '*.bpl' \) -print0
        fi
    done | sort -zu
}

# now_ms -- wall clock in milliseconds.
now_ms() { date +%s%3N; }
