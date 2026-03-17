ARG PASTTEL_HOME=/app/pasttel

FROM ubuntu:24.04

SHELL ["/bin/bash", "-c"]

ARG PASTTEL_HOME
ENV PASTTEL_HOME=${PASTTEL_HOME}
WORKDIR ${PASTTEL_HOME}

# Solver versions
ENV Z3_VERSION=4.16.0
ENV CVC5_VERSION=1.3.3

# PASTTEL and CVC5_DIR point to the solvers install prefix (used by Makefile)
ENV PASTTEL=${PASTTEL_HOME}/solvers
ENV CVC5_DIR=${PASTTEL_HOME}/solvers

ENV PATH=${PASTTEL_HOME}/bin:${PASTTEL}/bin:${PATH}
ENV LD_LIBRARY_PATH=${PASTTEL}/lib

RUN DEBIAN_FRONTEND=noninteractive \
    apt-get -y update \
    && apt-get -y upgrade \
    && apt-get install -y --no-install-recommends \
        build-essential \
        wget \
        unzip \
        ca-certificates \
        python3 \
        python3-pip \
    && pip3 install --break-system-packages unittest-xml-reporting \
    && rm -rf /var/lib/apt/lists/*

# Install Z3 (bin/libz3.so + bin/z3 + include/)
RUN mkdir -p ${PASTTEL}/include ${PASTTEL}/lib ${PASTTEL}/bin \
    && wget -q https://github.com/Z3Prover/z3/releases/download/z3-${Z3_VERSION}/z3-${Z3_VERSION}-x64-glibc-2.39.zip \
    && unzip -q z3-*.zip \
    && cp z3-${Z3_VERSION}-x64-glibc-2.39/include/*.h ${PASTTEL}/include/ \
    && cp z3-${Z3_VERSION}-x64-glibc-2.39/bin/libz3.so ${PASTTEL}/lib/ \
    && cp z3-${Z3_VERSION}-x64-glibc-2.39/bin/z3 ${PASTTEL}/bin/ \
    && rm -rf z3-*.zip z3-${Z3_VERSION}-x64-glibc-2.39/

# Install CVC5 (shared build: lib/ + include/ + bin/cvc5)
RUN wget -q https://github.com/cvc5/cvc5/releases/download/cvc5-${CVC5_VERSION}/cvc5-Linux-x86_64-shared.zip \
    && unzip -q cvc5-*.zip \
    && cp -r cvc5-Linux-x86_64-shared/include/* ${PASTTEL}/include/ \
    && cp    cvc5-Linux-x86_64-shared/lib/libcvc5.so*        ${PASTTEL}/lib/ \
    && cp    cvc5-Linux-x86_64-shared/lib/libcvc5parser.so*  ${PASTTEL}/lib/ \
    && cp    cvc5-Linux-x86_64-shared/lib/libpoly.so*        ${PASTTEL}/lib/ \
    && cp    cvc5-Linux-x86_64-shared/lib/libpolyxx.so*      ${PASTTEL}/lib/ \
    && cp    cvc5-Linux-x86_64-shared/lib/libgmp.so*         ${PASTTEL}/lib/ \
    && cp    cvc5-Linux-x86_64-shared/bin/cvc5               ${PASTTEL}/bin/ \
    && rm -rf cvc5-*.zip cvc5-Linux-x86_64-shared/

# Copy sources and build
COPY . .

RUN make -j$(nproc)

CMD ["./bin/pasttel"]
