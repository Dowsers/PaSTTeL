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
ULTIMATE_ULR_SHUFFLE="${ULTIMATE_ULR_SHUFFLE:-${TOOLS_DIR}/UAutomizer-linux-shuffle}"
ULTIMATE_UPL="${ULTIMATE_UPL:-${TOOLS_DIR}/UAutomizer-PaSTTeL-linux}"

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
}

# ultimate_has_pasttel <dir>
# True when the release's BuchiAutomizer plugin actually carries the PaSTTeL
# backend. Without this check a release built from upstream silently ignores the
# PASTTEL setting and falls back to LassoRanker, which would make a UPL-vs-ULR
# comparison measure ULR against itself.
ultimate_has_pasttel() {
    local dir="$1" jar
    jar=$(find "${dir}/plugins" -maxdepth 1 \
          -name 'de.uni_freiburg.informatik.ultimate.plugins.generator.buchiautomizer_*.jar' \
          2>/dev/null | head -1)
    [ -n "${jar}" ] || return 1
    # -Z1 lists entry names only. Plain 'unzip -l' also echoes the archive's own
    # path, which matches 'pasttel' for any release stored under this repository.
    unzip -Z1 "${jar}" 2>/dev/null | grep -qi 'pasttel'
}

# toolchain_for <file>  -- echoes the toolchain XML matching the input's extension.
toolchain_for() {
    case "${1##*.}" in
        c)   echo "${TOOLCHAIN_DIR}/BuchiAutomizerC.xml" ;;
        bpl) echo "${TOOLCHAIN_DIR}/BuchiAutomizerBpl.xml" ;;
        *)   return 1 ;;
    esac
}

# now_ms -- wall clock in milliseconds.
now_ms() { date +%s%3N; }
