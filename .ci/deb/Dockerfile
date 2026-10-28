ARG BASE_IMAGE=ubuntu:22.04

FROM ${BASE_IMAGE} AS deps
ENV DEBIAN_FRONTEND=noninteractive \
    TZ=UTC \
    LANG=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
        sudo ca-certificates curl wget git unzip dos2unix \
        build-essential cmake g++ make autoconf automake libtool pkg-config \
        uuid-dev libssl-dev libz-dev libbz2-dev libreadline-dev liblzma-dev libffi-dev \
        libboost-dev libboost-system-dev libboost-thread-dev \
        libpq-dev libcurl4-openssl-dev \
        python3 python3-dev python3-pip python3-setuptools python3-numpy python3-distutils-extra \
        sqlite3 \
        krb5-user \
        avahi-daemon \
        dpkg-dev

FROM deps AS builder

WORKDIR /build
COPY . /build/plugin

RUN find /build/plugin -type f -print0 \
        | xargs -0 -r dos2unix -q 2>/dev/null || true \
    && chmod +x /build/plugin/fledge/requirements.sh \
        /build/plugin/.ci/deb/generate_control.sh \
        /build/plugin/.ci/deb/package_deb.sh

WORKDIR /build/plugin/fledge
RUN bash ./requirements.sh
RUN make -j"$(nproc)"

WORKDIR /build/plugin
RUN if [ -f requirements.sh ]; then bash ./requirements.sh; fi

ENV FLEDGE_INSTALL=/usr/local/fledge
ENV LD_LIBRARY_PATH=/usr/local/fledge/lib

WORKDIR /build/plugin
RUN mkdir -p build && cd build \
    && cmake -DFLEDGE_SRC=/build/plugin/fledge -DFLEDGE_INSTALL="${FLEDGE_INSTALL}" .. \
    && make -j"$(nproc)" \
    && make install

RUN .ci/deb/package_deb.sh "${FLEDGE_INSTALL}" /artifacts

# Run the plugin unit tests (gtest) against the Fledge build above:
#   docker build -f .ci/deb/Dockerfile --target test .
FROM builder AS test

RUN apt-get update && apt-get install -y --no-install-recommends libgtest-dev googletest \
    && rm -rf /var/lib/apt/lists/*
# Some distros ship the gtest sources only
RUN if ! ls /usr/lib/*/libgtest.* /usr/lib/libgtest.* >/dev/null 2>&1; then \
        cmake -S /usr/src/googletest -B /tmp/gtest-build \
        && cmake --build /tmp/gtest-build -j"$(nproc)" \
        && cp /tmp/gtest-build/lib/*.a /usr/lib/; \
    fi

ENV FLEDGE_DATA=/tmp
ENV LD_LIBRARY_PATH=/build/plugin/fledge/cmake_build/C/lib:/usr/local/fledge/lib

WORKDIR /build/plugin
# Current gtest releases need C++14, the test CMakeLists pins C++11
RUN sed -i 's/-std=c++11/-std=c++14/' tests/CMakeLists.txt \
    && mkdir -p tests-build && cd tests-build \
    && cmake -DFLEDGE_SRC=/build/plugin/fledge ../tests \
    && make -j"$(nproc)"
RUN cd tests-build && ./RunTests

# Runnable Fledge (from the submodule) with this plugin installed, see
# docker-compose.yml in the repository root.
FROM builder AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends rsyslog curl \
    && rm -rf /var/lib/apt/lists/* \
    && sed -i '/imklog/d' /etc/rsyslog.conf

WORKDIR /build/plugin/fledge
RUN make install

# South sinusoid plugin, pinned to a tag and verified by commit
ARG SINUSOID_TAG=v3.1.0
ARG SINUSOID_COMMIT=4ff6eab5f21671fbcfd244e716572e699d0974da
RUN git clone --quiet --depth 1 --branch "${SINUSOID_TAG}" \
        https://github.com/fledge-iot/fledge-south-sinusoid.git /tmp/sinusoid \
    && test "$(git -C /tmp/sinusoid rev-parse HEAD)" = "${SINUSOID_COMMIT}" \
    && cp -R /tmp/sinusoid/python/fledge/plugins/south/sinusoid \
        /usr/local/fledge/python/fledge/plugins/south/ \
    && rm -rf /tmp/sinusoid

# Taken from the builder stage, where line endings have been normalised
RUN cp /build/plugin/.ci/compose/entrypoint.sh /usr/local/bin/entrypoint.sh \
    && chmod +x /usr/local/bin/entrypoint.sh

ENV FLEDGE_ROOT=/usr/local/fledge
EXPOSE 8081 1995
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

FROM scratch AS artifact
COPY --from=builder /artifacts/ /
