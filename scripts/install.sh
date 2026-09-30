#!/bin/bash
# install.sh -- Install PaSTTeL and everything the artifact's scripts need, without Docker.
#
# The Docker image is built by this very script (see the Dockerfile), so a machine it has set up
# runs the scripts exactly as the image does. Tested on Ubuntu 24.04 x86-64, the image's base; the
# solver archives of tools/solvers/ are x86-64 builds that need glibc 2.39 or newer.
#
#   1. System packages, with apt (needs root or sudo; skip with --no-apt on another distribution,
#      after installing the equivalents yourself): g++ and make, Boost headers, libedit, gawk, unzip,
#      Python 3 with plotly, and Java 21 for Ultimate.
#   2. Z3 4.16.0 and CVC5 1.3.4, from the archives of tools/solvers/, into --prefix: include/, lib/
#      and bin/ (z3, cvc5). Ultimate runs the z3 it finds on PATH, hence step 5.
#   3. PaSTTeL, built from pasttel/ against these solvers: pasttel/bin/pasttel.
#   4. A check: PaSTTeL proves the paper's Figure 1 lasso terminating, and Java and plotly are found.
#   5. The two lines to add to your shell, printed at the end.
#
# Usage:
#   bash scripts/install.sh [--prefix <dir>]   (default: pasttel/solvers, as in the image; PASTTEL is ignored)
#                           [--no-apt]         (step 1 skipped)
#
# Then run the smoke test: bash scripts/run_smoke_test.sh (README, section 2).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

# Inside the artifact by default, whatever PASTTEL says: a PASTTEL exported for another installation
# (the Makefile's own default is ~/.local) must not have its solvers overwritten by these.
PREFIX="${PASTTEL_HOME}/solvers"
APT=true
Z3_ZIP="${TOOLS_DIR}/solvers/z3-4.16.0-x64-glibc-2.39.zip"
CVC5_ZIP="${TOOLS_DIR}/solvers/cvc5-Linux-x86_64-shared.zip"
# libedit2: the cvc5 command-line binary needs it (PaSTTeL itself links the CVC5 library only).
PACKAGES=(build-essential libboost-dev libedit2 gawk unzip python3 python3-plotly openjdk-21-jre-headless)

usage() { sed -n "2,$(grep -n '^# Then run the smoke test' "${BASH_SOURCE[0]}" | cut -d: -f1)p" "${BASH_SOURCE[0]}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)  PREFIX="$2"; shift 2 ;;
        --no-apt)  APT=false;   shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

[ "$(uname -m)" = x86_64 ] || die "the solver archives and the Ultimate releases are x86-64 builds; this machine is $(uname -m)"
require_file "${Z3_ZIP}" "Z3 archive"
require_file "${CVC5_ZIP}" "CVC5 archive"
mkdir -p "${PREFIX}"
PREFIX="$(realpath "${PREFIX}")"

echo "[1/4] System packages"
if "${APT}"; then
    SUDO=""
    [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    ${SUDO} apt-get -y update
    ${SUDO} env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${PACKAGES[@]}"
    # split_specific.sh, which sorts the lasso traces, is written for GNU awk.
    ${SUDO} update-alternatives --install /usr/bin/awk awk /usr/bin/gawk 10
else
    echo "  skipped (--no-apt); needed: ${PACKAGES[*]}"
fi

echo "[2/4] Z3 and CVC5 into ${PREFIX}"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
mkdir -p "${PREFIX}/include" "${PREFIX}/lib" "${PREFIX}/bin"
unzip -q "${Z3_ZIP}" -d "${TMP}/z3"
Z3_DIR="$(find "${TMP}/z3" -mindepth 1 -maxdepth 1 -type d -print -quit)"
cp "${Z3_DIR}"/include/*.h "${PREFIX}/include/"
cp "${Z3_DIR}/bin/libz3.so" "${PREFIX}/lib/"
cp "${Z3_DIR}/bin/z3" "${PREFIX}/bin/"
unzip -q "${CVC5_ZIP}" -d "${TMP}/cvc5"
CVC5_SRC="${TMP}/cvc5/cvc5-Linux-x86_64-shared"
cp -r "${CVC5_SRC}"/include/* "${PREFIX}/include/"
for lib in libcvc5 libcvc5parser libpoly libpolyxx libgmp; do
    cp -P "${CVC5_SRC}/lib/${lib}".so* "${PREFIX}/lib/"
done
cp "${CVC5_SRC}/bin/cvc5" "${PREFIX}/bin/"

echo "[3/4] PaSTTeL (make -j$(nproc) in ${PASTTEL_HOME})"
# The Makefile links against ${PASTTEL} and ${CVC5_DIR}, and records them as the binary's run path.
make -C "${PASTTEL_HOME}" -j"$(nproc)" PASTTEL="${PREFIX}" CVC5_DIR="${PREFIX}"

echo "[4/4] Check"
# With the solvers just installed first, as the two lines printed below set them: an LD_LIBRARY_PATH
# naming another installation would otherwise take precedence over the binary's own run path.
export PATH="${PREFIX}/bin:${PATH}"
export LD_LIBRARY_PATH="${PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
require_java 21
python3 -c "import plotly" 2>/dev/null || die "python3 cannot import plotly (package python3-plotly)"
out="$("${PASTTEL_BIN}" -a both -s z3 -c 1 "${PASTTEL_HOME}/examples/figure1_paper_example.json" 2>&1)" \
    || die "pasttel failed on the paper's Figure 1 example:
${out}"
grep -q '^OVERALL RESULT: TERMINATING' <<< "${out}" \
    || die "pasttel did not prove the paper's Figure 1 example terminating:
${out}"
echo "  pasttel: Figure 1 example proved TERMINATING; z3 $("${PREFIX}/bin/z3" -version | sed 's/^Z3 version //; s/ - .*//')"

echo ""
echo "Installed. Add these two lines to your shell (or ~/.bashrc): Ultimate needs this z3 on PATH."
echo "  export PATH=\"${PREFIX}/bin:\${PATH}\""
echo "  export LD_LIBRARY_PATH=\"${PREFIX}/lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}\""
echo "Then: bash scripts/run_smoke_test.sh"
