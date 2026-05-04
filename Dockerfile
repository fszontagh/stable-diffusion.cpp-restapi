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
COPY . /src

# Narrow CUDA archs to shorten build time. Defaults cover most RunPod GPUs:
#   86 = RTX 3090 / A10 / A40
#   89 = RTX 4090 / L40
#   80 = A100
#   90 = H100
# Override at build time:
#   docker build --build-arg CMAKE_CUDA_ARCHITECTURES="89" -t sdcpp-restapi:cuda .
ARG CMAKE_CUDA_ARCHITECTURES="80;86;89;90"

# SD_EXPERIMENTAL_OFFLOAD=ON enables the forked sd.cpp's layer-streaming and
# dynamic offload modes — useful for fitting larger models (e.g. Z-Image bf16
# at ~12GB) onto smaller GPUs (16GB cards). It also avoids the broken
# upstream short-SHA pin in CMakeLists.txt:183 that fails with shallow clones.
# Runtime offload is opt-in: the binary defaults to offload_mode=none.
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

# Copy data/ next to the binary so architecture_manager.cpp's exe-relative
# fallback (search_paths[3]) finds model_architectures.json at runtime.
RUN mkdir -p /opt/sdcpp/data \
    && cp /src/data/model_architectures.json /opt/sdcpp/data/

# ============================================================================
# Stage 2: Runtime — slim image with binary + assets only
# ============================================================================
FROM nvidia/cuda:12.4.0-runtime-ubuntu22.04

ENV DEBIAN_FRONTEND=noninteractive

# Runtime deps from docs/DEPENDENCIES-ubuntu22.04-cuda.md + tini for PID 1
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 \
        libbrotli1 \
        libssl3 \
        zlib1g \
        ca-certificates \
        tini \
        curl \
        jq \
    && rm -rf /var/lib/apt/lists/*

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
