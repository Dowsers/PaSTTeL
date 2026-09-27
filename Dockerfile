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

# The three Ultimate releases live in /app/tools/, where scripts/common.sh finds them together with
# tools/settings/ and tools/toolchains/. No TOOLCHAIN_DIR here: it would override common.sh's default.
# ULTIMATE_HOME names the fixed-order release for scripts that still take a single release.
ENV ULTIMATE_HOME=/app/tools/UAutomizer-linux
ENV PATH=${PASTTEL_HOME}/bin:${PASTTEL}/bin:${ULTIMATE_HOME}:${PATH}
ENV LD_LIBRARY_PATH=${PASTTEL}/lib

RUN apt-get -y update \
    && apt-get -y upgrade \
    && apt-get install -y --no-install-recommends \
        build-essential \
        libboost-dev \
        gawk \
        wget \
        unzip \
        ca-certificates \
        python3 \
        python3-pip \
        python3-plotly \
        python3-pandas \
        python3-matplotlib \
        openjdk-21-jre-headless \
    && update-alternatives --install /usr/bin/awk awk /usr/bin/gawk 10 \
    && pip3 install --break-system-packages unittest-xml-reporting \
    && rm -rf /var/lib/apt/lists/*

# Install Z3 (bin/libz3.so + bin/z3 + include/) — from pre-downloaded archive (offline build)
# One Z3 for every tool in the image: PaSTTeL links this libz3, and the Ultimate releases ship no z3
# binary of their own, so Ultimate resolves "z3" through PATH to this ${PASTTEL}/bin/z3. The archive name
# is derived from Z3_VERSION, so the copied archive and the extracted path cannot disagree.
COPY tools/solvers/z3-${Z3_VERSION}-x64-glibc-2.39.zip /tmp/z3.zip
RUN mkdir -p ${PASTTEL}/include ${PASTTEL}/lib ${PASTTEL}/bin \
    && unzip -q /tmp/z3.zip -d /tmp/z3 \
    && cp /tmp/z3/z3-${Z3_VERSION}-x64-glibc-2.39/include/*.h ${PASTTEL}/include/ \
    && cp /tmp/z3/z3-${Z3_VERSION}-x64-glibc-2.39/bin/libz3.so ${PASTTEL}/lib/ \
    && cp /tmp/z3/z3-${Z3_VERSION}-x64-glibc-2.39/bin/z3 ${PASTTEL}/bin/ \
    && rm -rf /tmp/z3.zip /tmp/z3

# Install CVC5 (shared build: lib/ + include/ + bin/cvc5) — from pre-downloaded archive (offline build)
COPY tools/solvers/cvc5-Linux-x86_64-shared.zip /tmp/cvc5.zip
RUN unzip -q /tmp/cvc5.zip -d /tmp/cvc5 \
    && cp -r /tmp/cvc5/cvc5-Linux-x86_64-shared/include/* ${PASTTEL}/include/ \
    && cp    /tmp/cvc5/cvc5-Linux-x86_64-shared/lib/libcvc5.so*        ${PASTTEL}/lib/ \
    && cp    /tmp/cvc5/cvc5-Linux-x86_64-shared/lib/libcvc5parser.so*  ${PASTTEL}/lib/ \
    && cp    /tmp/cvc5/cvc5-Linux-x86_64-shared/lib/libpoly.so*        ${PASTTEL}/lib/ \
    && cp    /tmp/cvc5/cvc5-Linux-x86_64-shared/lib/libpolyxx.so*      ${PASTTEL}/lib/ \
    && cp    /tmp/cvc5/cvc5-Linux-x86_64-shared/lib/libgmp.so*         ${PASTTEL}/lib/ \
    && cp    /tmp/cvc5/cvc5-Linux-x86_64-shared/bin/cvc5               ${PASTTEL}/bin/ \
    && rm -rf /tmp/cvc5.zip /tmp/cvc5

# Ultimate releases (prebuilt from the ultimate/ submodule; none ships a z3, see above):
#   UAutomizer-linux           LassoRanker, fixed strategy order      -> ULR vs P-ULR
#   UAutomizer-linux-shuffler  LassoRanker, random strategy order     -> ULR vs P-ULR (randomised)
#   UAutomizer-PaSTTeL-linux   LassoRanker or PaSTTeL rank backend    -> ULR vs UPL
COPY tools/UAutomizer-linux/          /app/tools/UAutomizer-linux/
COPY tools/UAutomizer-linux-shuffler/ /app/tools/UAutomizer-linux-shuffler/
COPY tools/UAutomizer-PaSTTeL-linux/  /app/tools/UAutomizer-PaSTTeL-linux/
COPY tools/settings/                  /app/tools/settings/
COPY tools/toolchains/                /app/tools/toolchains/

# Copy PaSTTeL source and build
WORKDIR ${PASTTEL_HOME}
COPY pasttel/ .
RUN make -j$(nproc)

# Copy artifact scripts, benchmarks, and logs
COPY scripts/ /app/scripts/
COPY benchmarks/ /app/benchmarks/
COPY logs/ /app/logs/

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
