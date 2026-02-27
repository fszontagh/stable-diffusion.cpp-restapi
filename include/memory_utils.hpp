#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Memory information structure
 */
struct MemoryInfo {
    // System RAM (in bytes)
    uint64_t system_total = 0;
    uint64_t system_used = 0;
    uint64_t system_free = 0;

    // Process memory (in bytes)
    uint64_t process_rss = 0;      // Resident Set Size (physical memory used by process)
    uint64_t process_virtual = 0;  // Virtual memory size

    // GPU VRAM (in bytes) - only available if GPU backend enabled
    bool gpu_available = false;
    uint64_t gpu_total = 0;
    uint64_t gpu_used = 0;
    uint64_t gpu_free = 0;
    std::string gpu_name;

    /**
     * Convert to JSON for API/WebSocket
     */
    nlohmann::json to_json() const;

    /**
     * Get system memory usage percentage (0-100)
     */
    double system_usage_percent() const {
        if (system_total == 0) return 0.0;
        return (static_cast<double>(system_used) / system_total) * 100.0;
    }

    /**
     * Get GPU memory usage percentage (0-100)
     */
    double gpu_usage_percent() const {
        if (!gpu_available || gpu_total == 0) return 0.0;
        return (static_cast<double>(gpu_used) / gpu_total) * 100.0;
    }
};

/**
 * Get current memory information
 * @return MemoryInfo structure with current memory stats
 */
MemoryInfo get_memory_info();

/**
 * Format bytes as human-readable string (e.g., "1.5 GB")
 */
std::string format_bytes(uint64_t bytes);

} // namespace sdcpp
