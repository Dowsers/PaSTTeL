#!/bin/bash
# package.sh -- Build the VMCAI 2027 artifact archive, ready for upload to Zenodo.
#
# Produces, in dist/:
#   pasttel-artifact-vmcai27.tar.gz          the Docker image (docker save, gzip), also inside the archive
#   pasttel-vmcai27-artifact.tar.gz          the artifact archive, with a single top-level directory
#   *.sha256                                 their SHA256 checksums
#
# The archive holds an explicit list of paths (INCLUDE below) -- nothing else in the working tree ends
# up in it -- plus the Ultimate sources: `git archive` of the ultimate/ submodule at the commit it
# records, i.e. the fork that integrates PaSTTeL, without its multi-GB build products.
#
# Usage:
#   bash package.sh [--dry-run]    --dry-run: check and list what would be packaged, create nothing
#
# Prerequisite: the image, built with `docker build -t pasttel-artifact:vmcai27 .`

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

IMAGE="pasttel-artifact:vmcai27"
IMAGE_TAR="pasttel-artifact-vmcai27.tar.gz"
NAME="pasttel-vmcai27-artifact"
DIST="dist"
DRY_RUN=false
[ "${1:-}" = "--dry-run" ] && DRY_RUN=true

# Paths of the working tree that go into the archive; everything else stays out.
# In tools/, the solvers go in as their release archives only, not as the copies extracted next to them.
INCLUDE=(README LICENSE Dockerfile .dockerignore docker-compose.yml package.sh
         pasttel scripts benchmarks logs
         tools/UAutomizer-linux tools/UAutomizer-PaSTTeL-linux
         tools/settings tools/toolchains tools/solvers/*.zip)
[ -f paper.pdf ] && INCLUDE+=(paper.pdf)
# Build products and local leftovers inside the included paths.
EXCLUDE=(--exclude='pasttel/bin' --exclude='*.o' --exclude='*.d' --exclude='__pycache__'
         --exclude='*.pyc' --exclude='*.smt2' --exclude='lasso_traces')

warn() { echo "  Warning: $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }
human() { numfmt --to=iec --suffix=B "$1" 2>/dev/null || echo "$1 B"; }

echo "============================================================"
echo " PaSTTeL VMCAI 2027 artifact -- packaging$(${DRY_RUN} && echo ' (dry run)')"
echo "============================================================"

# -- Checks ---------------------------------------------------------------------
for p in "${INCLUDE[@]}"; do [ -e "${p}" ] || die "missing: ${p}"; done
[ -f paper.pdf ] || warn "paper.pdf not found: add it before the final submission."
docker image inspect "${IMAGE}" >/dev/null 2>&1 \
    || die "Docker image ${IMAGE} not found; build it first: docker build -t ${IMAGE} ."
ULTIMATE_COMMIT="$(git -C ultimate rev-parse HEAD)"
if [ -n "$(git -C ultimate status --porcelain --untracked-files=no)" ]; then
    warn "ultimate/ has uncommitted changes; the archive holds the commit ${ULTIMATE_COMMIT:0:10}, not them."
fi
# Placeholders left in the README are easy to miss once the archive is on Zenodo.
if grep -q "TO COMPLETE" README; then
    warn "README still has placeholders:"
    grep -n "TO COMPLETE" README | sed 's/^/             /' | cut -c1-110
fi

echo ""
echo "  Contents (top-level directory ${NAME}/):"
for p in "${INCLUDE[@]}"; do
    printf '    %-44s %8s\n' "${p}" "$(du -sh "${EXCLUDE[@]}" "${p}" 2>/dev/null | cut -f1)"
done
printf '    %-44s %8s   git archive of commit %s\n' "ultimate/" \
    "$(human "$(git -C ultimate archive HEAD | wc -c)")" "${ULTIMATE_COMMIT:0:10}"
printf '    %-44s %8s   docker save of %s (uncompressed)\n' "${IMAGE_TAR}" \
    "$(human "$(docker image inspect "${IMAGE}" --format '{{.Size}}')")" "${IMAGE}"

if ${DRY_RUN}; then
    echo ""
    echo "  Dry run: nothing created."
    exit 0
fi

command -v pigz >/dev/null && GZIP_CMD=(pigz -9) || GZIP_CMD=(gzip -9)
mkdir -p "${DIST}"
STAGE="$(mktemp -d "${DIST}/.stage.XXXXXX")"
trap 'rm -rf "${STAGE}"' EXIT

# -- 1. Docker image ------------------------------------------------------------
echo ""
echo "[1/3] docker save ${IMAGE} | ${GZIP_CMD[0]}  ->  ${DIST}/${IMAGE_TAR}"
docker save "${IMAGE}" | "${GZIP_CMD[@]}" > "${DIST}/${IMAGE_TAR}"
(cd "${DIST}" && sha256sum "${IMAGE_TAR}" > "${IMAGE_TAR}.sha256")

# -- 2. Archive -----------------------------------------------------------------
echo "[2/3] ${NAME}.tar.gz"
TAR="${STAGE}/${NAME}.tar"
tar --create --file "${TAR}" "${EXCLUDE[@]}" --transform "s|^|${NAME}/|" "${INCLUDE[@]}"
tar --append --file "${TAR}" --directory "${DIST}" --transform "s|^|${NAME}/|" "${IMAGE_TAR}"
git -C ultimate archive --format=tar --prefix="${NAME}/ultimate/" HEAD > "${STAGE}/ultimate.tar"
tar --concatenate --file "${TAR}" "${STAGE}/ultimate.tar"
"${GZIP_CMD[@]}" < "${TAR}" > "${DIST}/${NAME}.tar.gz"
(cd "${DIST}" && sha256sum "${NAME}.tar.gz" > "${NAME}.tar.gz.sha256")

# -- 3. Report ------------------------------------------------------------------
echo "[3/3] Done"
echo ""
echo "============================================================"
printf ' %-38s %8s  sha256 %s\n' "${DIST}/${NAME}.tar.gz" "$(du -sh "${DIST}/${NAME}.tar.gz" | cut -f1)" \
    "$(cut -d' ' -f1 "${DIST}/${NAME}.tar.gz.sha256")"
printf ' %-38s %8s  sha256 %s\n' "${DIST}/${IMAGE_TAR}" "$(du -sh "${DIST}/${IMAGE_TAR}" | cut -f1)" \
    "$(cut -d' ' -f1 "${DIST}/${IMAGE_TAR}.sha256")"
echo " Ultimate sources: commit ${ULTIMATE_COMMIT} of the ultimate/ submodule"
echo "============================================================"
echo " Upload ${NAME}.tar.gz to Zenodo, and give EasyChair the DOI of that version"
echo " (not the concept DOI) with the SHA256 above."
echo "============================================================"
