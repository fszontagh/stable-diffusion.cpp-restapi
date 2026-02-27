#include "memory_utils.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <iomanip>

#ifdef __linux__
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

#ifdef SDCPP_USE_CUDA
#include <cuda_runtime.h>
#endif

#ifdef SDCPP_USE_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace sdcpp {

nlohmann::json MemoryInfo::to_json() const {
    nlohmann::json j;

    // System memory
    j["system"] = {
        {"total_bytes", system_total},
        {"used_bytes", system_used},
        {"free_bytes", system_free},
        {"total_mb", system_total / (1024 * 1024)},
        {"used_mb", system_used / (1024 * 1024)},
        {"free_mb", system_free / (1024 * 1024)},
        {"usage_percent", system_usage_percent()}
    };

    // Process memory
    j["process"] = {
        {"rss_bytes", process_rss},
        {"virtual_bytes", process_virtual},
        {"rss_mb", process_rss / (1024 * 1024)},
        {"virtual_mb", process_virtual / (1024 * 1024)}
    };

    // GPU memory (if available)
    j["gpu"] = {
        {"available", gpu_available},
        {"name", gpu_name},
        {"total_bytes", gpu_total},
        {"used_bytes", gpu_used},
        {"free_bytes", gpu_free},
        {"total_mb", gpu_total / (1024 * 1024)},
        {"used_mb", gpu_used / (1024 * 1024)},
        {"free_mb", gpu_free / (1024 * 1024)},
        {"usage_percent", gpu_usage_percent()}
    };

    return j;
}

#ifdef __linux__
static void read_proc_meminfo(MemoryInfo& info) {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) return;

    std::string line;
    uint64_t mem_total = 0, mem_free = 0, mem_available = 0, buffers = 0, cached = 0;

    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t value;
        std::string unit;

        iss >> key >> value >> unit;

        if (key == "MemTotal:") mem_total = value * 1024;
        else if (key == "MemFree:") mem_free = value * 1024;
        else if (key == "MemAvailable:") mem_available = value * 1024;
        else if (key == "Buffers:") buffers = value * 1024;
        else if (key == "Cached:") cached = value * 1024;
    }

    info.system_total = mem_total;
    // Use MemAvailable if present (more accurate), otherwise calculate
    if (mem_available > 0) {
        info.system_free = mem_available;
    } else {
        info.system_free = mem_free + buffers + cached;
    }
    info.system_used = info.system_total - info.system_free;
}

static void read_proc_self_status(MemoryInfo& info) {
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) return;

    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream iss(line.substr(6));
            uint64_t value;
            iss >> value;
            info.process_rss = value * 1024;  // Convert from kB to bytes
        } else if (line.compare(0, 7, "VmSize:") == 0) {
            std::istringstream iss(line.substr(7));
            uint64_t value;
            iss >> value;
            info.process_virtual = value * 1024;
        }
    }
}
#endif

#ifdef SDCPP_USE_CUDA
static void read_cuda_memory(MemoryInfo& info) {
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    if (err != cudaSuccess || device_count == 0) {
        return;
    }

    // Use device 0 (primary GPU)
    size_t free_bytes = 0, total_bytes = 0;
    err = cudaMemGetInfo(&free_bytes, &total_bytes);
    if (err != cudaSuccess) {
        return;
    }

    cudaDeviceProp prop;
    err = cudaGetDeviceProperties(&prop, 0);
    if (err == cudaSuccess) {
        info.gpu_name = prop.name;
    }

    info.gpu_available = true;
    info.gpu_total = total_bytes;
    info.gpu_free = free_bytes;
    info.gpu_used = total_bytes - free_bytes;
}
#endif

#ifdef SDCPP_USE_VULKAN
static void read_vulkan_memory(MemoryInfo& info) {
    // Vulkan memory query is more complex and requires an initialized device
    // For now, we'll mark GPU as available but without detailed memory info
    // The actual VRAM usage would need to be tracked by the application

    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(VK_NULL_HANDLE, &device_count, nullptr);

    // Note: This requires a valid VkInstance, which we don't have access to here
    // The Vulkan backend in ggml would need to expose memory info
    // For now, we skip Vulkan memory reporting
    (void)result;
    (void)device_count;
}
#endif

MemoryInfo get_memory_info() {
    MemoryInfo info;

#ifdef __linux__
    read_proc_meminfo(info);
    read_proc_self_status(info);
#endif

#ifdef SDCPP_USE_CUDA
    read_cuda_memory(info);
#endif

    // Note: Vulkan memory reporting requires more infrastructure
    // For now, only CUDA provides GPU memory info

    return info;
}

std::string format_bytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[unit_index];
    return oss.str();
}

} // namespace sdcpp
