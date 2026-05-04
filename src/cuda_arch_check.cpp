#ifdef SDCPP_USE_CUDA

#include "cuda_arch_check.hpp"

#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SDCPP_CUDA_ARCHS
#define SDCPP_CUDA_ARCHS ""
#endif

namespace sdcpp::cuda {

namespace {

std::vector<int> parse_arch_list(const std::string& s) {
    std::vector<int> archs;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find_first_of(";, ", start);
        if (end == std::string::npos) end = s.size();
        std::string token = s.substr(start, end - start);
        // Strip CMake suffixes like "86-real" / "86-virtual" / "all-major"
        size_t dash = token.find('-');
        if (dash != std::string::npos) token = token.substr(0, dash);
        try {
            if (!token.empty()) archs.push_back(std::stoi(token));
        } catch (...) {
            // Skip non-numeric tokens (e.g. "all", "native") — best-effort
        }
        start = end + 1;
    }
    return archs;
}

std::string join_archs(const std::vector<int>& archs) {
    std::ostringstream out;
    for (size_t i = 0; i < archs.size(); ++i) {
        if (i) out << ", ";
        out << "sm_" << archs[i];
    }
    return out.str();
}

}  // namespace

ArchCheckResult check_arch_compatibility() {
    ArchCheckResult result;
    result.compiled_archs = parse_arch_list(SDCPP_CUDA_ARCHS);

    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
        result.ok = true;  // No CUDA device — let downstream code fail with its own error
        result.message = "No CUDA devices detected; skipping arch check.";
        return result;
    }

    int device = 0;
    cudaGetDevice(&device);

    cudaDeviceProp props{};
    if (cudaGetDeviceProperties(&props, device) != cudaSuccess) {
        result.ok = true;  // Couldn't query — don't block startup, let runtime fail naturally
        result.message = "Could not query CUDA device properties; skipping arch check.";
        return result;
    }

    result.device_name = props.name;
    result.device_compute_cap = props.major * 10 + props.minor;

    // If the binary has no recorded arch list (compiled with empty SDCPP_CUDA_ARCHS),
    // we can't check; assume ok and warn.
    if (result.compiled_archs.empty()) {
        result.ok = true;
        result.message = "Binary was compiled without a recorded CUDA arch list "
                         "(SDCPP_CUDA_ARCHS empty); skipping arch check.";
        return result;
    }

    // The binary's kernels run on a device if compute_cap >= some compiled arch.
    // (Newer GPUs can run older PTX-compiled kernels via PTX JIT, but only if
    //  the arch was compiled in at least PTX form. CMake's CMAKE_CUDA_ARCHITECTURES
    //  defaults to a mix of -code=sm_X (binary) and -code=compute_X (PTX), so a
    //  GPU one or two minor-versions newer than the highest compiled arch is
    //  generally fine via PTX JIT. But going UP is fine; going DOWN — running
    //  on a sm_86 GPU when only sm_89+ kernels are compiled — fails hard.)
    // The safest check: device's compute_cap must equal or exceed the LOWEST
    // compiled arch, AND must not be lower than any of them by a generation gap
    // (because PTX JIT only forward-compatibles, never backward).
    int min_compiled = *std::min_element(result.compiled_archs.begin(),
                                         result.compiled_archs.end());
    if (result.device_compute_cap < min_compiled) {
        std::ostringstream msg;
        msg << "GPU '" << result.device_name << "' has compute capability sm_"
            << result.device_compute_cap
            << ", but this binary was compiled only for ["
            << join_archs(result.compiled_archs)
            << "]. Kernels will fail to dispatch ('no kernel image is "
               "available for execution on the device') at first GPU op.\n"
            << "  Fix: rebuild the image adding sm_" << result.device_compute_cap
            << " to CMAKE_CUDA_ARCHITECTURES.\n"
            << "  Example: docker build --build-arg "
            << "CMAKE_CUDA_ARCHITECTURES=\"" << SDCPP_CUDA_ARCHS
            << ";" << result.device_compute_cap << "\" ...";
        result.ok = false;
        result.message = msg.str();
        return result;
    }

    // Device cap is >= min compiled — runnable, possibly via PTX JIT.
    result.ok = true;
    return result;
}

int get_device_compute_capability() {
    static int cached = -2;  // -2 = not yet queried, -1 = no device
    if (cached != -2) return cached;

    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
        cached = -1;
        return cached;
    }
    int device = 0;
    cudaGetDevice(&device);
    cudaDeviceProp props{};
    if (cudaGetDeviceProperties(&props, device) != cudaSuccess) {
        cached = -1;
        return cached;
    }
    cached = props.major * 10 + props.minor;
    return cached;
}

namespace {

std::string lower_basename(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return name;
}

// Best-effort scan: peek at a safetensors header to find the first tensor's
// dtype string. Returns empty if not a safetensors or read fails. The header
// format is: 8-byte LE u64 length, then a JSON object whose keys are tensor
// names and values are { dtype, shape, data_offsets }.
std::string peek_safetensors_dtype(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    uint64_t header_len = 0;
    f.read(reinterpret_cast<char*>(&header_len), sizeof(header_len));
    if (!f || header_len == 0 || header_len > 100ull * 1024 * 1024) return {};
    std::string header(static_cast<size_t>(header_len), '\0');
    f.read(header.data(), static_cast<std::streamsize>(header_len));
    if (!f) return {};
    try {
        auto j = nlohmann::json::parse(header);
        for (auto& [name, info] : j.items()) {
            if (name == "__metadata__") continue;
            if (info.is_object() && info.contains("dtype")) {
                return info["dtype"].get<std::string>();
            }
        }
    } catch (...) {
        // Not a valid safetensors header, or unexpected JSON shape — fall through
    }
    return {};
}

bool dtype_is_nvfp4(const std::string& dtype) {
    if (dtype.empty()) return false;
    std::string upper = dtype;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    // Known NVFP4 dtype strings vary by tooling: NVFP4, FP4_E2M1, F4_E2M1, FP4
    return upper.find("NVFP4") != std::string::npos ||
           upper.find("FP4_E2M1") != std::string::npos ||
           upper.find("F4_E2M1") != std::string::npos;
}

bool dtype_is_fp8(const std::string& dtype) {
    if (dtype.empty()) return false;
    std::string upper = dtype;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return upper.find("F8_E4M3") != std::string::npos ||
           upper.find("F8_E5M2") != std::string::npos ||
           upper.find("FP8_E4M3") != std::string::npos ||
           upper.find("FP8_E5M2") != std::string::npos;
}

}  // namespace

ModelFormatCheck check_model_format_compatibility(const std::string& path) {
    ModelFormatCheck result;

    int cap = get_device_compute_capability();
    if (cap < 0) {
        // No CUDA device — let downstream code handle it; not our problem
        return result;
    }

    std::string name = lower_basename(path);
    bool name_is_nvfp4 = name.find("nvfp4") != std::string::npos;
    bool name_is_fp8   = name.find("fp8")   != std::string::npos ||
                         name.find("e4m3")  != std::string::npos ||
                         name.find("e5m2")  != std::string::npos;

    // For safetensors, also peek at the actual dtype — filename match alone
    // is heuristic; the dtype field is authoritative when available.
    std::string dtype;
    if (name.size() >= 12 &&
        name.compare(name.size() - 12, 12, ".safetensors") == 0) {
        dtype = peek_safetensors_dtype(path);
    }
    bool dtype_nvfp4 = dtype_is_nvfp4(dtype);
    bool dtype_fp8   = dtype_is_fp8(dtype);

    if ((name_is_nvfp4 || dtype_nvfp4) && cap < 100) {
        std::ostringstream msg;
        msg << "This file ('" << name
            << "') uses NVFP4 quantization, which requires a Blackwell GPU "
               "(sm_100+, e.g. RTX 5090). The active GPU has compute "
               "capability sm_" << cap
            << "; on Ampere/Ada (sm_8x), NVFP4 weights dequantize to fp16 "
               "at load time — the file's on-disk size triples in VRAM and "
               "typically OOMs before any offload mode can engage.";
        result.ok = false;
        result.message = msg.str();
        result.suggested_alternative =
            "Use a Q8_0 GGUF (~6.6 GB for Z-Image) or BF16 GGUF (~12.3 GB) "
            "instead — both run natively on sm_8x.";
        return result;
    }

    if ((name_is_fp8 || dtype_fp8) && cap < 89) {
        std::ostringstream msg;
        msg << "This file ('" << name
            << "') uses FP8 quantization, which requires sm_89+ (RTX 4090, "
               "L40, H100). The active GPU has compute capability sm_" << cap
            << "; on older cards, FP8 dequantizes to fp16 at load time and "
               "uses ~2x the on-disk size in VRAM.";
        result.ok = false;
        result.message = msg.str();
        result.suggested_alternative =
            "Use a Q8_0 GGUF or BF16/FP16 GGUF instead — both run natively "
            "on sm_8x.";
        return result;
    }

    return result;
}

}  // namespace sdcpp::cuda

#endif  // SDCPP_USE_CUDA
