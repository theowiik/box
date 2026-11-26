# The tools to develop the box, in one image: ESP-IDF at the commit the
# ESP-Mosaico workspace pins, CMake and gcc for the core's tests, and Go for
# box-essentials. The workspace itself is not in it: toolchain/fetch checks it
# out in .deps/ and the container mounts it. toolchain/run builds and starts
# this with the pins in toolchain/versions; mise.toml's tasks go through that.
FROM ubuntu:24.04

# ESP-IDF's documented prerequisites, plus what the tests and tools need.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        git wget curl ca-certificates flex bison gperf cmake ninja-build ccache \
        python3 python3-pip python3-venv libffi-dev libssl-dev dfu-util libusb-1.0-0 \
        build-essential \
    && rm -rf /var/lib/apt/lists/*

# The exact ESP-IDF commit the workspace requires (toolchain/versions).
ARG IDF_COMMIT
ENV IDF_PATH=/opt/esp/idf IDF_TOOLS_PATH=/opt/esp/tools
RUN git init -q $IDF_PATH && cd $IDF_PATH \
    && git remote add origin https://github.com/espressif/esp-idf.git \
    && git fetch -q --depth 1 origin $IDF_COMMIT && git checkout -q FETCH_HEAD \
    && git submodule update -q --init --recursive --depth 1 \
    && ./install.sh esp32s31 \
    && rm -rf $IDF_TOOLS_PATH/dist

# Go, for box-essentials.
ARG GO_VERSION
ARG TARGETARCH
RUN curl -fsSL https://go.dev/dl/go${GO_VERSION}.linux-${TARGETARCH:-amd64}.tar.gz | tar -C /usr/local -xz
ENV PATH=/usr/local/go/bin:$PATH CGO_ENABLED=0

# The container runs with your uid; give it a writable home for caches
# (Go's build cache, the ESP component manager's downloads, box-essentials'
# settings when run here), kept in a volume between containers. The Mosaico
# tools' own state (which gateway holds the board) stays in the container: it
# is only true while that container's gateway runs.
ENV HOME=/home/dev GOCACHE=/home/dev/.cache/go GOPATH=/home/dev/go \
    BOX_STATE_DIR=/home/dev/.cache/box-essentials
RUN mkdir -p /home/dev/.cache && chmod -R 1777 /home/dev

COPY entrypoint.sh /usr/local/bin/box-env
COPY mosaico /usr/local/bin/mosaico
# toolchain/run sets MOSAICO_WORKSPACE to the checkout in .deps/.
ENTRYPOINT ["/usr/local/bin/box-env"]
CMD ["bash"]
