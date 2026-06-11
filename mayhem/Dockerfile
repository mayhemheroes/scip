# syntax=docker/dockerfile:1.7
FROM ghcr.io/mayhemheroes/base:2026-06
ARG COMMIT REPO_URL SOURCE_DATE_EPOCH SANITIZER_FLAGS MAYHEM_JOBS
USER root
RUN repro-sources-list.sh 2>/dev/null || true; \
    apt-get update && apt-get install -yq --no-install-recommends \
      cmake g++ gcc wget ca-certificates libgmp-dev zlib1g-dev \
      libboost-program-options-dev libboost-serialization-dev libboost-regex-dev \
      libboost-iostreams-dev libtbb-dev libreadline-dev pkg-config git \
      liblapack-dev libgsl-dev flex bison libopenblas-dev clang \
    && rm -rf /var/lib/apt/lists/*
ENV LD_LIBRARY_PATH=/mayhem/install/lib
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem
RUN bash mayhem/build.sh
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi
LABEL org.opencontainers.image.source="${REPO_URL}" org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" rlenv.user="mayhem"
ENV LD_LIBRARY_PATH=/mayhem/install/lib
USER mayhem
WORKDIR /mayhem
