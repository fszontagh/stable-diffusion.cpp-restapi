# syntax=docker/dockerfile:1.7
# ============================================================================
# sdcpp-restapi container image (CUDA + WebUI + MCP)
#
# Build:
#   docker build -t sdcpp-restapi:cuda .
#
# Run (locally, requires nvidia-container-toolkit):
#   docker run --gpus all -p 8080:8080 \
#     -v /path/to/models:/workspace/models \
#     -v /path/to/output:/workspace/output \
#     sdcpp-restapi:cuda
#
# RunPod usage:
#   1. Push to a registry (Docker Hub, GHCR)
#   2. Create a Pod with this image, attach a Network Volume mounted at /workspace
#   3. Expose HTTP port 8080
# ============================================================================

# ============================================================================
# Stage 1: Builder — compile sdcpp-restapi + Vue WebUI
# ============================================================================
FROM nvidia/cuda:12.4.0-devel-ubuntu22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        curl \
        ca-certificates \
        pkg-config \
        libssl-dev \
        zlib1g-dev \
        libbrotli-dev \
    && rm -rf /var/lib/apt/lists/*

# Node.js 20 for the Vue WebUI build (vue-tsc + vite)
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# COPYs ordered by stability (least → most frequently edited) for cache reuse.
# Editing a file only invalidates its layer + everything after it.

# 1. CMake config + version (rarely changes)
COPY CMakeLists.txt VERSION /src/

# 2. C++ source — invalidating these correctly forces a recompile
COPY src/ /src/src/
COPY include/ /src/include/

# 3. WebUI source — invalidating these forces npm install + vite build
COPY webui/ /src/webui/

# 4. Docs — needed by CMake's docs-copy custom target (part of ALL)
COPY docs/ /src/docs/

# Narrow CUDA archs to shorten build time. Defaults cover most RunPod GPUs:
#   80 = A100 datacenter
#   86 = RTX 3090, A4000-A6000, A40, RTX A4500
#   89 = RTX 4090, RTX 4080, L40
#   90 = H100, H200
# Override at build time:
#   docker build --build-arg CMAKE_CUDA_ARCHITECTURES="89" -t sdcpp-restapi:cuda .
# Building for a single arch is ~3-4x faster but the binary only runs on that
# specific GPU class — narrowing without verifying the deploy target risks
# "no kernel image is available for execution" at first GPU op (sm mismatch).
ARG CMAKE_CUDA_ARCHITECTURES="80;86;89;90"

# SD_EXPERIMENTAL_OFFLOAD=ON enables the forked sd.cpp's layer-streaming and
# dynamic offload modes — useful for fitting larger models (e.g. Z-Image bf16
# at ~12 GB) onto smaller GPUs (16-20 GB cards). Pinned in CMakeLists.txt to
# the May 4 streaming speedup commit. Runtime offload is opt-in: the binary
# defaults to offload_mode=none.
RUN cmake -S /src -B /src/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DSD_CUDA=ON \
        -DSD_EXPERIMENTAL_OFFLOAD=ON \
        -DSDCPP_WEBUI=ON \
        -DSDCPP_WEBSOCKET=ON \
        -DSDCPP_MCP=ON \
        -DSDCPP_ASSISTANT=ON \
        -DCMAKE_CUDA_ARCHITECTURES="${CMAKE_CUDA_ARCHITECTURES}" \
    && cmake --build /src/build -j"$(nproc)" \
    && cmake --install /src/build --prefix /opt/sdcpp

# 5. Data — runtime-only (model_architectures.json read at startup, not compile).
# Copied AFTER the cmake build so editing data/ files doesn't invalidate the
# ~25-min compile cache. The bootstrap_models.json is similarly copied late
# in stage 2 (runtime image).
COPY data/ /src/data/
RUN mkdir -p /opt/sdcpp/data \
    && cp /src/data/model_architectures.json /opt/sdcpp/data/

# ============================================================================
# Stage 2: Runtime — slim image with binary + assets only
# ============================================================================
FROM nvidia/cuda:12.4.0-runtime-ubuntu22.04

ENV DEBIAN_FRONTEND=noninteractive

# Runtime deps from docs/DEPENDENCIES-ubuntu22.04-cuda.md + tini for PID 1.
# openssh-server provides SSH access on port 22 (RunPod auto-injects the
# user's pubkey to /root/.ssh/authorized_keys on pod start; entrypoint.sh
# starts sshd alongside the API server). Without this, exposing port 22 in
# the deploy config returns "Connection refused" because nothing's listening.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 \
        libbrotli1 \
        libssl3 \
        zlib1g \
        ca-certificates \
        tini \
        curl \
        jq \
        openssh-server \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /run/sshd /root/.ssh \
    && chmod 700 /root/.ssh \
    && sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin prohibit-password/' /etc/ssh/sshd_config \
    && sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/' /etc/ssh/sshd_config \
    && sed -i 's/^#\?PubkeyAuthentication.*/PubkeyAuthentication yes/' /etc/ssh/sshd_config

COPY --from=builder /opt/sdcpp /opt/sdcpp

COPY runpod-config.json /etc/sdcpp-restapi/config.json
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
COPY scripts/bootstrap-models.sh /opt/sdcpp/bin/bootstrap-models.sh
COPY data/bootstrap_models.json /opt/sdcpp/data/bootstrap_models.json
RUN chmod +x /usr/local/bin/entrypoint.sh /opt/sdcpp/bin/bootstrap-models.sh

ENV PATH=/opt/sdcpp/bin:$PATH \
    SDCPP_DOCS_PATH=/opt/sdcpp/share/sdcpp-restapi/docs \
    SDCPP_VOLUME_ROOT=/workspace

EXPOSE 8080

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/entrypoint.sh"]
