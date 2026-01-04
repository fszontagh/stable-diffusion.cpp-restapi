#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace sdcpp {
namespace utils {

/**
 * Generate a UUID v4 string
 * @return UUID string in format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 */
std::string generate_uuid();

/**
 * Compute SHA256 hash of a file
 * @param filepath Path to file
 * @return Hex-encoded SHA256 hash
 * @throws std::runtime_error if file cannot be read
 */
std::string compute_sha256(const std::string& filepath);

/**
 * Base64 encode binary data
 * @param data Pointer to data
 * @param len Length of data
 * @return Base64 encoded string
 */
std::string base64_encode(const uint8_t* data, size_t len);

/**
 * Base64 encode a vector
 * @param data Vector of bytes
 * @return Base64 encoded string
 */
std::string base64_encode(const std::vector<uint8_t>& data);

/**
 * Base64 decode string to binary data
 * @param encoded Base64 encoded string
 * @return Decoded binary data
 * @throws std::runtime_error if input is invalid
 */
std::vector<uint8_t> base64_decode(const std::string& encoded);

/**
 * Get file extension (lowercase, without dot)
 * @param filepath Path to file
 * @return Extension string (e.g., "safetensors", "gguf")
 */
std::string get_file_extension(const std::string& filepath);

/**
 * Get filename without path
 * @param filepath Full path
 * @return Filename only
 */
std::string get_filename(const std::string& filepath);

/**
 * Get file size in bytes
 * @param filepath Path to file
 * @return Size in bytes, or 0 if file doesn't exist
 */
size_t get_file_size(const std::string& filepath);

/**
 * Check if file exists
 * @param filepath Path to file
 * @return true if file exists
 */
bool file_exists(const std::string& filepath);

/**
 * Check if directory exists
 * @param dirpath Path to directory
 * @return true if directory exists
 */
bool directory_exists(const std::string& dirpath);

/**
 * Create directory recursively
 * @param dirpath Path to directory
 * @return true if created or already exists
 */
bool create_directory(const std::string& dirpath);

/**
 * Get current timestamp as ISO8601 string
 * @return Timestamp string (e.g., "2024-01-01T12:00:00Z")
 */
std::string get_timestamp();

/**
 * Get current timestamp as time_point
 */
std::chrono::system_clock::time_point get_time_now();

/**
 * Convert time_point to ISO8601 string
 */
std::string time_to_string(const std::chrono::system_clock::time_point& tp);

/**
 * Parse ISO8601 string to time_point
 */
std::chrono::system_clock::time_point string_to_time(const std::string& str);

/**
 * List files in directory with specific extensions
 * @param dirpath Directory path
 * @param extensions List of extensions to filter (e.g., {".safetensors", ".gguf"})
 * @param recursive Whether to search subdirectories
 * @return List of file paths relative to dirpath
 */
std::vector<std::string> list_files(
    const std::string& dirpath,
    const std::vector<std::string>& extensions,
    bool recursive = true
);

/**
 * Sanitize filename for safe use
 * @param filename Input filename
 * @return Sanitized filename
 */
std::string sanitize_filename(const std::string& filename);

} // namespace utils
} // namespace sdcpp
