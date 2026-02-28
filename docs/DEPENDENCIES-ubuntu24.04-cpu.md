# Ubuntu 24.04 CPU Build Dependencies

This document lists the runtime dependencies required to run sdcpp-restapi CPU builds on Ubuntu 24.04.

## Variants Covered

- `noavx` - For very old CPUs or VMs without AVX support
- `avx` - For CPUs with AVX support (Intel Sandy Bridge 2011+, AMD Bulldozer+)
- `avx2` - For modern CPUs with AVX2 (Intel Haswell 2013+, AMD Excavator+)
- `avx512` - For CPUs with AVX-512 (Intel Skylake-X, Ice Lake+)

## Required Packages

```bash
sudo apt-get update
sudo apt-get install -y libgomp1 libbrotli1 libssl3t64 zlib1g
```

### Package Details

| Package | Description |
|---------|-------------|
| `libgomp1` | OpenMP runtime library for parallel processing |
| `libbrotli1` | Brotli compression library |
| `libssl3t64` | OpenSSL cryptographic library (Ubuntu 24.04 naming) |
| `zlib1g` | Compression library |

> **Note:** Ubuntu 24.04 uses `libssl3t64` instead of `libssl3` due to the time64 ABI transition.

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

- Ubuntu 24.04 LTS (Docker container `ubuntu:24.04`)
- All four CPU variants (noavx, avx, avx2, avx512) verified working
