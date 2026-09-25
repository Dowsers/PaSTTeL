#!/bin/bash
# package.sh — Create the final artifact ZIP for ATVA 2026 submission.
#
# Usage: bash package.sh
# Output: pasttel-atva26-artifact.zip + SHA256 checksum
#
# Before running this script, generate the Docker image tar.gz manually:
#   docker build -t pasttel-artifact .
#   docker save pasttel-artifact | gzip > pasttel-artifact-docker-image.tar.gz
#
# Excludes: .git/, Jenkinsfile, todo.txt, output/ (local generated results),
#           compiled .o files, Python bytecode, and the ZIP itself.

set -e

ARTIFACT_NAME="pasttel-atva26-artifact.zip"
DOCKER_TAR="pasttel-artifact-docker-image.tar.gz"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "============================================================"
echo " PaSTTeL Artifact — Packaging"
echo "============================================================"
echo " Root     : ${SCRIPT_DIR}"
echo " Output   : ${ARTIFACT_NAME}"
echo "============================================================"
echo ""

cd "${SCRIPT_DIR}"

# Warn if paper.pdf is missing
if [ ! -f "paper.pdf" ]; then
    echo "  Warning: paper.pdf not found. Add it before final submission."
fi

# Warn if Docker image tar.gz is missing
if [ ! -f "${DOCKER_TAR}" ]; then
    echo "  Warning: ${DOCKER_TAR} not found."
    echo "  Generate it first with:"
    echo "    docker build -t pasttel-artifact ."
    echo "    docker save pasttel-artifact | gzip > ${DOCKER_TAR}"
fi

echo ""
echo "  Creating ${ARTIFACT_NAME}..."

rm -f "${ARTIFACT_NAME}"

zip -r "${ARTIFACT_NAME}" . \
    --exclude="*.git*" \
    --exclude="*/.git/*" \
    --exclude="*/.git" \
    --exclude="*/output/*" \
    --exclude="*/__pycache__/*" \
    --exclude="*.pyc" \
    --exclude="*/src/*.o" \
    --exclude="${ARTIFACT_NAME}"

echo ""
echo "  Done."
echo ""
echo "============================================================"
echo " Artifact ZIP : ${ARTIFACT_NAME}"
echo " Size         : $(du -sh "${ARTIFACT_NAME}" | cut -f1)"
echo " SHA256       : $(sha256sum "${ARTIFACT_NAME}" | cut -d' ' -f1)"
echo "============================================================"
echo ""
echo "  The ZIP contains ${DOCKER_TAR}."
echo "  Reviewers load the Docker image with:"
echo "    docker load --input ${DOCKER_TAR}"
echo "============================================================"
