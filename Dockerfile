ARG PASTTEL_HOME=/app/pasttel

FROM ubuntu:24.04

SHELL ["/bin/bash", "-c"]

ENV DEBIAN_FRONTEND=noninteractive

ARG PASTTEL_HOME
ENV PASTTEL_HOME=${PASTTEL_HOME}

# Solver versions
ENV Z3_VERSION=4.16.0
ENV CVC5_VERSION=1.3.4

# PASTTEL and CVC5_DIR point to the solvers install prefix (used by Makefile)
ENV PASTTEL=${PASTTEL_HOME}/solvers
ENV CVC5_DIR=${PASTTEL_HOME}/solvers

# The two Ultimate releases live in /app/tools/, where scripts/common.sh finds them together with
# tools/settings/ and tools/toolchains/. No TOOLCHAIN_DIR here: it would override common.sh's default.
# ULTIMATE_HOME names the release of ULR-Baseline vs P-ULR for scripts that still take a single release.
ENV ULTIMATE_HOME=/app/tools/UAutomizer-linux
ENV PATH=${PASTTEL_HOME}/bin:${PASTTEL}/bin:${ULTIMATE_HOME}:${PATH}
ENV LD_LIBRARY_PATH=${PASTTEL}/lib

# System packages, Z3 and CVC5 from tools/solvers/, and PaSTTeL built against them: all done by
# scripts/install.sh, the script that installs the artifact without Docker (README, section 6), so the
# image and a machine set up by hand are the same. One Z3 for every tool: PaSTTeL links its libz3, and
# the Ultimate releases ship no z3 binary of their own, so Ultimate resolves "z3" through PATH to
# ${PASTTEL}/bin/z3.
COPY tools/solvers/z3-${Z3_VERSION}-x64-glibc-2.39.zip tools/solvers/cvc5-Linux-x86_64-shared.zip /app/tools/solvers/
COPY scripts/install.sh scripts/common.sh /app/scripts/
COPY pasttel/ ${PASTTEL_HOME}/
RUN bash /app/scripts/install.sh && rm -rf /var/lib/apt/lists/*
# PaSTTeL's test suite (247 tests, both solvers, every ranking function re-checked with -val on part of
# them): the image is not built if one fails.
RUN cd ${PASTTEL_HOME} && python3 scripts/test_non_regression.py

# Prebuilt Ultimate releases (none ships a z3, see above):
#   UAutomizer-linux           LassoRanker, dumps the lasso traces    -> ULR-Baseline vs P-ULR
#   UAutomizer-PaSTTeL-linux   LassoRanker or PaSTTeL rank backend    -> Ultimate-LR vs Ultimate-PL (from ultimate/)
COPY tools/UAutomizer-linux/          /app/tools/UAutomizer-linux/
COPY tools/UAutomizer-PaSTTeL-linux/  /app/tools/UAutomizer-PaSTTeL-linux/
COPY tools/settings/                  /app/tools/settings/
COPY tools/toolchains/                /app/tools/toolchains/

# Copy artifact scripts, benchmarks, and logs
COPY scripts/ /app/scripts/
COPY benchmarks/ /app/benchmarks/
# Our runs: the CSVs and scatter plots of [1] and [2], their raw logs (zips), the smoke test's expected output
COPY logs/*.csv logs/*.html logs/*.zip logs/*.log /app/logs/

RUN chmod +x /app/scripts/*.sh \
    && mkdir -p /app/output

# Reduce JVM heap for the build-time smoke test (default 12G is too large for
# a Docker build layer). The full 12G limit is restored for interactive use.
RUN sed -i 's/-Xmx12G/-Xmx4G/' /app/tools/UAutomizer-*/Ultimate.ini

WORKDIR /app

# Validate that PaSTTeL and Ultimate work correctly before finalising the image
RUN bash /app/scripts/run_smoke_test.sh

# Restore full JVM heap for production use
RUN sed -i 's/-Xmx4G/-Xmx12G/' /app/tools/UAutomizer-*/Ultimate.ini

CMD ["/bin/bash"]
