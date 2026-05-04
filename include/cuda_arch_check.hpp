#pragma once

#include <string>
#include <vector>

#ifdef SDCPP_USE_CUDA

namespace sdcpp::cuda {

struct ArchCheckResult {
    bool ok = false;
    int device_compute_cap = 0;        // e.g. 86 for sm_86
    std::string device_name;
    std::vector<int> compiled_archs;   // e.g. {80, 86, 89, 90}
    std::string message;               // human-readable explanation (empty if ok)
};

// Inspects the active CUDA device and compares its compute capability against
// the list of CUDA architectures this binary was compiled for (baked in via
// SDCPP_CUDA_ARCHS at compile time). Returns ok=true if the device is
// compatible. Returns ok=false with a populated message if the binary won't
// be able to dispatch kernels on this device.
//
// Has no side effects beyond reading CUDA device properties — does not
// dispatch kernels, allocate memory, or initialize backends.
ArchCheckResult check_arch_compatibility();

// Returns the active CUDA device's compute capability as a single int
// (e.g. 86 for sm_8.6). Returns -1 if no CUDA device is available or the
// query fails. Cached on first call.
int get_device_compute_capability();

struct ModelFormatCheck {
    bool ok = true;
    std::string message;   // empty if ok; user-facing explanation otherwise
    std::string suggested_alternative;  // e.g. "use Q8_0 GGUF" / "use BF16 GGUF"
};

// Heuristic check: does the file at `path` (typically a model weights file)
// use a quantization format the active GPU can't run natively? If so,
// loading would either OOM (load-time dequantize-to-fp16) or hard-crash at
// first kernel dispatch — neither produces a useful error.
//
// Currently checks for:
//   - NVFP4 (filename match + safetensors header dtype scan): needs sm_100+
//   - FP8 (filename match): needs sm_89+
//
// Returns ok=true if compatible (or unknown — defaults to permissive so we
// don't block legit loads). Returns ok=false with an actionable message
// pointing at a working alternative when a known incompatibility is found.
ModelFormatCheck check_model_format_compatibility(const std::string& path);

}  // namespace sdcpp::cuda

#endif  // SDCPP_USE_CUDA
