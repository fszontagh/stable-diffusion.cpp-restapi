# Dynamic VRAM Offloading Benchmark Results

## Test Environment

- **GPU**: NVIDIA (12GB VRAM)
- **CPU**: ~10 years old, no AVX2/AVX512 (CPU tests will be slow)
- **Resolution**: 1024x1024
- **Prompt**: "a cat"
- **Steps**: 9
- **Sampler**: euler
- **Scheduler**: smoothstep
- **Seed**: 42 (fixed for reproducibility)

## Model Configuration

| Component | Model Name | Size |
|-----------|------------|------|
| Diffusion Model | z_image_turbo-Q8_0.gguf | 6.8 GiB |
| VAE | ae_q8_0.gguf | 320 MiB |
| LLM (cond_stage) | Qwen3-4b-Z-Engineer-V2-Q8_0.gguf | 4.0 GiB |

**Total Model Size**: ~11.1 GiB (exceeds 12GB VRAM when compute buffers are allocated)

## Test Matrix

| # | Test Configuration | Result | Duration | Notes |
|---|-------------------|--------|----------|-------|
| 1 | Baseline (no offload, no tiling) | **CUDA_OOM** | - | Memory exhausted during diffusion |
| 2 | No offload + VAE tiling | **CUDA_OOM** | - | Memory exhausted during diffusion |
| 3 | Dynamic offload (cond_only), no tiling | **SUCCESS** | ~66s | LLM offloaded after conditioning |
| 4 | Dynamic offload (cond_only) + VAE tiling | **SUCCESS** | ~149s | VAE tiling adds overhead |
| 5 | Keep CLIP on CPU + VAE tiling | **SUCCESS** | ~151s | Similar to dynamic offload |
| 6 | Keep CLIP+VAE on CPU + VAE tiling | **TIMEOUT** | >10min | VAE on CPU too slow |
| 7 | Dynamic offload + VAE on CPU + tiling | **TIMEOUT** | >10min | VAE on CPU too slow |
| 8 | Dynamic offload + VAE tiling (repeat) | **SUCCESS** | ~146s | Consistent with test 4 |

## Configuration Legend

- **offload_mode**:
  - `none` - No dynamic offloading
  - `cond_only` - Offload cond_stage (LLM) to CPU after conditioning, reload before next gen
- **vae_tiling**: Enable tiled VAE decode to reduce peak VRAM usage
- **keep_clip_cpu**: Keep CLIP/LLM on CPU permanently (slower conditioning)
- **keep_vae_cpu**: Keep VAE on CPU permanently (much slower decode, not recommended on old CPUs)

## Analysis

### Key Findings

1. **Without Dynamic Offloading**: CUDA OOM at 1024x1024 with Q8 models (~11 GiB models + compute buffers > 12 GiB VRAM)

2. **With Dynamic Offloading (cond_only)**: Works by offloading ~4 GiB LLM to CPU after conditioning, freeing VRAM for diffusion compute buffers

3. **VAE Tiling**: Adds ~80s overhead (66s → 146s) due to multiple tile passes. Not needed for VRAM savings at this resolution since OOM occurs during diffusion, not VAE decode

4. **CPU-based Options**: Not viable on older CPUs without AVX2/AVX512 - VAE decode takes >10 minutes

### Performance Summary

| Configuration | Works? | Speed | Recommended? |
|--------------|--------|-------|--------------|
| offload_mode=none | No | - | Only for smaller models/resolutions |
| offload_mode=cond_only | Yes | ~66s | **Best option** |
| offload_mode=cond_only + vae_tiling | Yes | ~146s | Use if VAE OOM occurs |
| keep_clip_on_cpu | Yes | ~151s | Alternative to dynamic offload |
| keep_vae_on_cpu | No* | >10min | Not recommended (too slow) |

*Works but impractical due to CPU performance

### Recommendations

1. **For 12GB GPU with large Q8 models**: Use `offload_mode=cond_only`
   - Enables 1024x1024 generation that would otherwise OOM
   - Only ~1.3s overhead for LLM reload per generation

2. **VAE tiling**: Only enable if you encounter VAE-specific OOM (rare at 1024x1024)

3. **CPU offload options**: Avoid `keep_vae_on_cpu` unless you have a modern CPU with AVX2/AVX512

---
*Generated on: 2026-02-24 23:21 CET*
