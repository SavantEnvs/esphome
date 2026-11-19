# syntax=docker/dockerfile:1.7
#
# Commit image for ESPHome: the device firmware's C++ (esphome/core + components) built for the
# `host` platform with ASan+UBSan libFuzzer harnesses, plus upstream's C++ unit tests (gtest).
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile --build-arg COMMIT=$(git rev-parse HEAD) -t esphome .
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# googletest/googlemock for upstream's C++ unit tests (tests/components/*/*.cpp).
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends libgtest-dev libgmock-dev \
 && rm -rf /var/lib/apt/lists/*

# ArduinoJson (header-only), the version esphome/components/json/__init__.py pins with
# cg.add_library("bblanchon/ArduinoJson", "7.4.3"); upstream's build fetches it from the
# PlatformIO registry, so it is cached here for the offline rebuild (SPEC 6.2 item 19).
ARG ARDUINOJSON_VER=7.4.3
ARG ARDUINOJSON_SHA256=a2a8a277fc3cd0404499199bf1468682ea3688f354be0eaf6af8648366911257
RUN d=/opt/toolchains/deps/arduinojson-${ARDUINOJSON_VER} && mkdir -p "$d/src" \
 && curl -fsSL -o /tmp/dep.tgz https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v${ARDUINOJSON_VER}.tar.gz \
 && echo "${ARDUINOJSON_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
 && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" \
 && rm -f /tmp/dep.tgz && chmod -R a+rX "$d"

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
