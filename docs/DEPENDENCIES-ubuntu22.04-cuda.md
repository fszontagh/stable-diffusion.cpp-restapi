# Ubuntu 22.04 CUDA Build Dependencies

This document lists the runtime dependencies required to run sdcpp-restapi CUDA builds on Ubuntu 22.04.

## Variants Covered

- `cuda` - Standard NVIDIA CUDA support
- `cuda-experimental` - CUDA with experimental dynamic tensor offloading (for low-VRAM GPUs)

## System Requirements

- NVIDIA GPU with CUDA Compute Capability 5.0 or higher
- NVIDIA driver version 525 or later
- CUDA runtime 12.x (included in the nvidia/cuda Docker images)

## Required Packages

```bash
sudo apt-get update
sudo apt-get install -y libgomp1 libbrotli1 libssl3 zlib1g
```

### Package Details

| Package | Description |
|---------|-------------|
| `libgomp1` | OpenMP runtime library for parallel processing |
| `libbrotli1` | Brotli compression library |
| `libssl3` | OpenSSL cryptographic library |
| `zlib1g` | Compression library |

## CUDA Runtime

The CUDA builds link against CUDA 12.4 libraries. These are typically provided by:
- The NVIDIA driver package (for runtime use)
- NVIDIA CUDA container images (for Docker deployment)

### Docker Usage

For containerized deployment, use the NVIDIA CUDA runtime images:

```bash
docker run --gpus all -v /path/to/sdcpp:/app nvidia/cuda:12.4.0-runtime-ubuntu22.04 \
    /app/bin/sdcpp-restapi --help
```

## Verification

After installing dependencies, verify the binary runs:

```bash
./bin/sdcpp-restapi --version
```

Expected output:
```
sdcpp-restapi version X.X.X
Built with stable-diffusion.cpp
```

## Tested On

- Docker container `nvidia/cuda:12.4.0-runtime-ubuntu22.04`
- Both CUDA variants (cuda, cuda-experimental) verified working
- Tested with NVIDIA GeForce RTX 3060
