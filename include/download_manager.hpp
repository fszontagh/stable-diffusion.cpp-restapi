#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Download source type
 */
enum class DownloadSource {
    DirectURL,
    CivitAI,
    HuggingFace
};

/**
 * Download progress callback
 * @param downloaded_bytes Current downloaded bytes
 * @param total_bytes Total bytes (0 if unknown)
 * @param speed_bps Download speed in bytes per second
 */
using DownloadProgressCallback = std::function<void(size_t downloaded_bytes, size_t total_bytes, size_t speed_bps)>;

/**
 * Download result
 */
struct DownloadResult {
    bool success = false;
    std::string file_path;          // Full path to downloaded file
    std::string file_name;          // File name only
    std::string error_message;
    size_t file_size = 0;
    std::string content_type;       // MIME type from response

    // Metadata from source (CivitAI/HuggingFace)
    nlohmann::json metadata;
};

/**
 * CivitAI model info
 */
struct CivitAIModelInfo {
    int model_id = 0;
    int version_id = 0;
    std::string name;
    std::string version_name;
    std::string type;               // Checkpoint, LORA, VAE, etc.
    std::string base_model;         // SD 1.5, SDXL, etc.
    std::string download_url;
    std::string filename;
    size_t file_size = 0;
    std::string sha256;             // Expected hash
    nlohmann::json full_response;   // Full API response
};

/**
 * HuggingFace model info
 */
struct HuggingFaceModelInfo {
    std::string repo_id;            // e.g., "stabilityai/stable-diffusion-xl-base-1.0"
    std::string filename;           // e.g., "sd_xl_base_1.0.safetensors"
    std::string revision;           // Branch/tag, default "main"
    std::string download_url;
    size_t file_size = 0;
    nlohmann::json full_response;
};

/**
 * Supported model file extensions
 */
const std::vector<std::string> SUPPORTED_MODEL_EXTENSIONS = {
    ".safetensors", ".ckpt", ".pt", ".pth", ".bin", ".gguf"
};

/**
 * Download Manager - handles model downloads from various sources
 */
class DownloadManager {
public:
    /**
     * Constructor
     * @param paths_config Paths configuration for model directories
     */
    explicit DownloadManager(const nlohmann::json& paths_config);

    /**
     * Download a model from a direct URL
     * @param url Download URL
     * @param model_type Model type (checkpoint, vae, lora, etc.)
     * @param subfolder Optional subfolder within model type directory
     * @param filename Optional custom filename (auto-detected if empty)
     * @param progress_callback Optional progress callback
     * @return Download result
     */
    DownloadResult download_from_url(
        const std::string& url,
        const std::string& model_type,
        const std::string& subfolder = "",
        const std::string& filename = "",
        DownloadProgressCallback progress_callback = nullptr
    );

    /**
     * Download a model from CivitAI by model ID
     * @param model_id CivitAI model ID (or model_id:version_id)
     * @param model_type Model type for storage location
     * @param subfolder Optional subfolder
     * @param progress_callback Optional progress callback
     * @return Download result
     */
    DownloadResult download_from_civitai(
        const std::string& model_id,
        const std::string& model_type,
        const std::string& subfolder = "",
        DownloadProgressCallback progress_callback = nullptr
    );

    /**
     * Download a model from HuggingFace
     * @param repo_id HuggingFace repo ID (e.g., "stabilityai/sdxl-turbo")
     * @param filename File to download from the repo
     * @param model_type Model type for storage location
     * @param subfolder Optional subfolder
     * @param revision Optional revision (branch/tag), default "main"
     * @param progress_callback Optional progress callback
     * @return Download result
     */
    DownloadResult download_from_huggingface(
        const std::string& repo_id,
        const std::string& filename,
        const std::string& model_type,
        const std::string& subfolder = "",
        const std::string& revision = "main",
        DownloadProgressCallback progress_callback = nullptr
    );

    /**
     * Get model info from CivitAI without downloading
     * @param model_id CivitAI model ID (or model_id:version_id)
     * @return Model info if found
     */
    std::optional<CivitAIModelInfo> get_civitai_info(const std::string& model_id);

    /**
     * Get model info from HuggingFace without downloading
     * @param repo_id HuggingFace repo ID
     * @param filename File to get info for
     * @param revision Optional revision
     * @return Model info if found
     */
    std::optional<HuggingFaceModelInfo> get_huggingface_info(
        const std::string& repo_id,
        const std::string& filename,
        const std::string& revision = "main"
    );

    /**
     * Validate if a URL points to a model file
     * @param url URL to validate
     * @return true if URL appears to be a valid model file
     */
    bool validate_model_url(const std::string& url);

    /**
     * Check if a file extension is supported
     * @param extension File extension (with or without dot)
     * @return true if supported
     */
    static bool is_supported_extension(const std::string& extension);

    /**
     * Get the directory path for a model type
     * @param model_type Model type string
     * @return Directory path
     */
    std::string get_model_directory(const std::string& model_type) const;

    /**
     * Parse download source from identifier
     * @param identifier URL or ID string
     * @return Detected source type
     */
    static DownloadSource detect_source(const std::string& identifier);

private:
    nlohmann::json paths_config_;

    /**
     * Internal download with progress tracking
     */
    DownloadResult download_file(
        const std::string& url,
        const std::string& dest_path,
        const std::string& expected_filename,
        DownloadProgressCallback progress_callback
    );

    /**
     * Ensure directory exists, create if needed
     */
    bool ensure_directory(const std::string& path);

    /**
     * Extract filename from URL or Content-Disposition header
     */
    std::string extract_filename(const std::string& url, const std::string& content_disposition);

    /**
     * Make HTTP GET request and return response
     */
    std::string http_get(const std::string& url, int timeout_seconds = 30);
};

// String conversions
std::string download_source_to_string(DownloadSource source);
DownloadSource string_to_download_source(const std::string& str);

} // namespace sdcpp
