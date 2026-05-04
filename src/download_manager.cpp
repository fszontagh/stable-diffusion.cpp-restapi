#include "download_manager.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <curl/curl.h>

namespace fs = std::filesystem;

namespace sdcpp {

// String conversions
std::string download_source_to_string(DownloadSource source) {
    switch (source) {
        case DownloadSource::DirectURL: return "url";
        case DownloadSource::CivitAI: return "civitai";
        case DownloadSource::HuggingFace: return "huggingface";
        default: return "unknown";
    }
}

DownloadSource string_to_download_source(const std::string& str) {
    if (str == "url" || str == "direct") return DownloadSource::DirectURL;
    if (str == "civitai") return DownloadSource::CivitAI;
    if (str == "huggingface" || str == "hf") return DownloadSource::HuggingFace;
    return DownloadSource::DirectURL;
}

DownloadManager::DownloadManager(const nlohmann::json& paths_config)
    : paths_config_(paths_config) {
}

bool DownloadManager::is_supported_extension(const std::string& extension) {
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto& supported : SUPPORTED_MODEL_EXTENSIONS) {
        if (ext == supported) return true;
    }
    return false;
}

DownloadSource DownloadManager::detect_source(const std::string& identifier) {
    // Check for CivitAI patterns
    if (identifier.find("civitai.com") != std::string::npos ||
        std::regex_match(identifier, std::regex("^\\d+$")) ||
        std::regex_match(identifier, std::regex("^\\d+:\\d+$"))) {
        return DownloadSource::CivitAI;
    }

    // Check for HuggingFace patterns
    if (identifier.find("huggingface.co") != std::string::npos ||
        identifier.find("hf.co") != std::string::npos ||
        std::regex_match(identifier, std::regex("^[\\w-]+/[\\w.-]+$"))) {
        return DownloadSource::HuggingFace;
    }

    // Default to direct URL
    return DownloadSource::DirectURL;
}

std::string DownloadManager::get_model_directory(const std::string& model_type) const {
    // Map model type string to config path
    static const std::map<std::string, std::string> type_to_key = {
        {"checkpoint", "checkpoints"},
        {"checkpoints", "checkpoints"},
        {"diffusion", "diffusion_models"},
        {"diffusion_models", "diffusion_models"},
        {"vae", "vae"},
        {"lora", "lora"},
        {"loras", "lora"},
        {"clip", "clip"},
        {"t5", "t5"},
        {"t5xxl", "t5"},
        {"embedding", "embeddings"},
        {"embeddings", "embeddings"},
        {"controlnet", "controlnet"},
        {"llm", "llm"},
        {"esrgan", "esrgan"},
        {"upscaler", "esrgan"},
        {"taesd", "taesd"}
    };

    std::string lower_type = model_type;
    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);

    auto it = type_to_key.find(lower_type);
    if (it != type_to_key.end()) {
        if (paths_config_.contains(it->second)) {
            return paths_config_[it->second].get<std::string>();
        }
    }

    // Fallback: try direct key
    if (paths_config_.contains(model_type)) {
        return paths_config_[model_type].get<std::string>();
    }

    throw std::runtime_error("Unknown model type: " + model_type);
}

bool DownloadManager::ensure_directory(const std::string& path) {
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
        return fs::is_directory(path);
    } catch (const std::exception& e) {
        std::cerr << "[DownloadManager] Failed to create directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

std::string DownloadManager::extract_filename(const std::string& url, const std::string& content_disposition) {
    // Try Content-Disposition header first
    if (!content_disposition.empty()) {
        std::regex filename_regex("filename[*]?=[\"']?([^\"';\\s]+)[\"']?", std::regex::icase);
        std::smatch match;
        if (std::regex_search(content_disposition, match, filename_regex) && match.size() > 1) {
            return match[1].str();
        }
    }

    // Extract from URL path
    std::string path = url;

    // Remove query string
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }

    // Get last path component
    size_t last_slash = path.rfind('/');
    if (last_slash != std::string::npos && last_slash < path.length() - 1) {
        return path.substr(last_slash + 1);
    }

    return "model.safetensors";
}

namespace {

// Write callback for libcurl: appends to a std::string (for small responses)
size_t curl_write_string(void* data, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t bytes = size * nmemb;
    str->append(static_cast<const char*>(data), bytes);
    return bytes;
}

// State carried through the streaming write callback for file downloads
struct StreamState {
    std::ofstream* ofs = nullptr;
    size_t downloaded = 0;
    size_t total = 0;
    DownloadProgressCallback progress_callback;
    std::chrono::steady_clock::time_point start_time;
};

// Write callback for libcurl: writes chunks to disk and reports progress
size_t curl_write_stream(void* data, size_t size, size_t nmemb, void* userp) {
    auto* state = static_cast<StreamState*>(userp);
    size_t bytes = size * nmemb;

    if (state->ofs) {
        state->ofs->write(static_cast<const char*>(data), bytes);
        if (!*state->ofs) {
            // Returning fewer bytes than received tells libcurl to abort
            // (CURLE_WRITE_ERROR), which propagates as a download failure.
            return 0;
        }
    }

    state->downloaded += bytes;

    if (state->progress_callback) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state->start_time).count();
        size_t speed = elapsed > 0 ? state->downloaded / static_cast<size_t>(elapsed) : state->downloaded;
        state->progress_callback(state->downloaded, state->total, speed);
    }

    return bytes;
}

// Header callback: captures Content-Disposition (Content-Length and Content-Type
// are read via CURLINFO_* after the transfer; only Content-Disposition needs a
// header callback because there's no dedicated CURLINFO_ for it).
size_t curl_capture_disposition(char* buffer, size_t size, size_t nitems, void* userp) {
    auto* disposition = static_cast<std::string*>(userp);
    size_t bytes = size * nitems;
    constexpr const char* kPrefix = "content-disposition:";
    constexpr size_t kPrefixLen = 20; // strlen("content-disposition:")

    if (bytes >= kPrefixLen) {
        // Case-insensitive prefix match
        bool match = true;
        for (size_t i = 0; i < kPrefixLen; ++i) {
            char c = buffer[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != kPrefix[i]) { match = false; break; }
        }
        if (match) {
            std::string value(buffer + kPrefixLen, bytes - kPrefixLen);
            // Trim leading whitespace and trailing CRLF
            size_t start = value.find_first_not_of(" \t");
            size_t end = value.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                *disposition = value.substr(start, end - start + 1);
            }
        }
    }

    return bytes;
}

// Apply common cURL options used by every request the manager makes.
void apply_common_curl_options(CURL* curl, long connect_timeout, long total_timeout) {
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SDCpp-RestAPI/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    if (total_timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout);
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    // Use system CA bundle (default). TLS verification stays ON — cpp-httplib's
    // disabled-verification path was a security hole; libcurl + system CAs
    // validates HuggingFace's CloudFront cert correctly.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
}

} // anonymous namespace

std::string DownloadManager::http_get(const std::string& url, int timeout_seconds) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("HTTP request failed: curl_easy_init returned null");
    }

    std::string body;
    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    apply_common_curl_options(curl, 30L, static_cast<long>(timeout_seconds));

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        std::string msg = errbuf[0] ? std::string(errbuf) : std::string(curl_easy_strerror(rc));
        if (http_code) {
            throw std::runtime_error("HTTP " + std::to_string(http_code) + ": " + msg);
        }
        throw std::runtime_error("HTTP request failed: " + msg);
    }

    if (http_code != 200) {
        throw std::runtime_error("HTTP " + std::to_string(http_code));
    }

    return body;
}

bool DownloadManager::validate_model_url(const std::string& url) {
    // Check if URL has a supported extension
    std::string lower_url = url;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);

    // Remove query string for extension check
    size_t query_pos = lower_url.find('?');
    if (query_pos != std::string::npos) {
        lower_url = lower_url.substr(0, query_pos);
    }

    for (const auto& ext : SUPPORTED_MODEL_EXTENSIONS) {
        if (lower_url.length() >= ext.length() &&
            lower_url.substr(lower_url.length() - ext.length()) == ext) {
            return true;
        }
    }

    return false;
}

DownloadResult DownloadManager::download_file(
    const std::string& url,
    const std::string& dest_path,
    const std::string& expected_filename,
    DownloadProgressCallback progress_callback
) {
    DownloadResult result;

    // ----- HEAD request: probe size, content-type, content-disposition -----
    size_t total_size = 0;
    std::string content_type;
    std::string content_disposition;
    {
        CURL* curl = curl_easy_init();
        if (!curl) {
            result.error_message = "Download failed: curl_easy_init returned null";
            return result;
        }

        char errbuf[CURL_ERROR_SIZE] = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_capture_disposition);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &content_disposition);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
        // HEAD uses a shorter total timeout — the body is empty so 30s is plenty.
        apply_common_curl_options(curl, 30L, 60L);

        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_OK) {
            curl_off_t cl = 0;
            if (curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl) == CURLE_OK && cl > 0) {
                total_size = static_cast<size_t>(cl);
            }
            char* ct = nullptr;
            if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct) {
                content_type = ct;
            }
        }
        // HEAD failure isn't fatal — some servers/CDNs reject HEAD on signed
        // URLs. We still attempt the GET below.
        curl_easy_cleanup(curl);
    }

    // Validate content type - should be binary
    if (!content_type.empty() &&
        content_type.find("text/html") != std::string::npos &&
        content_type.find("application") == std::string::npos) {
        result.error_message = "URL does not point to a binary file (Content-Type: " + content_type + ")";
        return result;
    }

    // Determine filename
    std::string filename = expected_filename;
    if (filename.empty()) {
        filename = extract_filename(url, content_disposition);
    }

    // Validate extension
    fs::path file_path(filename);
    if (!is_supported_extension(file_path.extension().string())) {
        result.error_message = "Unsupported file extension: " + file_path.extension().string();
        return result;
    }

    // Create full destination path
    std::string full_path = (fs::path(dest_path) / filename).string();

    // Check if file already exists
    if (fs::exists(full_path)) {
        result.error_message = "File already exists: " + full_path;
        return result;
    }

    // ----- Streaming GET to disk -----
    std::ofstream ofs(full_path, std::ios::binary);
    if (!ofs) {
        result.error_message = "Failed to create file: " + full_path;
        return result;
    }

    StreamState state;
    state.ofs = &ofs;
    state.total = total_size;
    state.progress_callback = progress_callback;
    state.start_time = std::chrono::steady_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl) {
        ofs.close();
        fs::remove(full_path);
        result.error_message = "Download failed: curl_easy_init returned null";
        return result;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_stream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    // No CURLOPT_TIMEOUT for the body transfer — large model files (10+ GB)
    // can legitimately take hours on slow links. We rely on LOW_SPEED_LIMIT
    // to abort genuinely-stalled connections.
    apply_common_curl_options(curl, 30L, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);   // abort if <10 B/s for 60s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 10L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // If we didn't have a Content-Length up front, populate content_type from
    // the GET response (some servers omit it on HEAD).
    if (content_type.empty()) {
        char* ct = nullptr;
        if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct) {
            content_type = ct;
        }
    }

    curl_easy_cleanup(curl);
    ofs.close();

    if (rc != CURLE_OK) {
        fs::remove(full_path); // Clean up partial file
        std::string msg = errbuf[0] ? std::string(errbuf) : std::string(curl_easy_strerror(rc));
        result.error_message = "Download failed: " + msg;
        if (http_code) {
            result.error_message += " (HTTP " + std::to_string(http_code) + ")";
        }
        return result;
    }

    if (http_code != 0 && http_code != 200) {
        fs::remove(full_path);
        result.error_message = "Download failed with HTTP " + std::to_string(http_code);
        return result;
    }

    // Verify downloaded file
    if (!fs::exists(full_path) || fs::file_size(full_path) == 0) {
        fs::remove(full_path);
        result.error_message = "Downloaded file is empty or missing";
        return result;
    }

    result.success = true;
    result.file_path = full_path;
    result.file_name = filename;
    result.file_size = fs::file_size(full_path);
    result.content_type = content_type;

    return result;
}

DownloadResult DownloadManager::download_from_url(
    const std::string& url,
    const std::string& model_type,
    const std::string& subfolder,
    const std::string& filename,
    DownloadProgressCallback progress_callback
) {
    DownloadResult result;

    try {
        // Get base directory for model type
        std::string base_dir = get_model_directory(model_type);

        // Add subfolder if specified
        std::string dest_dir = base_dir;
        if (!subfolder.empty()) {
            dest_dir = (fs::path(base_dir) / subfolder).string();
        }

        // Ensure directory exists
        if (!ensure_directory(dest_dir)) {
            result.error_message = "Failed to create directory: " + dest_dir;
            return result;
        }

        // Download file
        result = download_file(url, dest_dir, filename, progress_callback);

        if (result.success) {
            result.metadata = {
                {"source", "url"},
                {"original_url", url},
                {"model_type", model_type},
                {"subfolder", subfolder}
            };
        }

    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    return result;
}

std::optional<CivitAIModelInfo> DownloadManager::get_civitai_info(const std::string& model_id) {
    try {
        // Parse model ID (can be "12345" or "12345:67890" for specific version)
        int mid = 0;
        int vid = 0;

        size_t colon_pos = model_id.find(':');
        if (colon_pos != std::string::npos) {
            mid = std::stoi(model_id.substr(0, colon_pos));
            vid = std::stoi(model_id.substr(colon_pos + 1));
        } else {
            mid = std::stoi(model_id);
        }

        nlohmann::json json;
        nlohmann::json version;
        std::string model_name;
        std::string model_type;

        // Try to fetch by model ID first
        bool model_fetch_success = false;
        try {
            std::string api_url = "https://civitai.com/api/v1/models/" + std::to_string(mid);
            std::string response = http_get(api_url, 30);
            json = nlohmann::json::parse(response);
            model_fetch_success = true;
            model_name = json.value("name", "");
            model_type = json.value("type", "");

            // Get the requested version or latest
            auto& versions = json["modelVersions"];
            if (!versions.empty()) {
                if (vid > 0) {
                    for (const auto& v : versions) {
                        if (v.value("id", 0) == vid) {
                            version = v;
                            break;
                        }
                    }
                } else {
                    version = versions[0];  // Latest version
                }
            }
        } catch (const std::exception& e) {
            // Model fetch failed - will try version endpoint if no colon in ID
            std::cerr << "[DownloadManager] Model fetch failed: " << e.what() << std::endl;
        }

        // If model fetch failed and ID has no colon, try as version ID
        if (!model_fetch_success && colon_pos == std::string::npos) {
            std::cerr << "[DownloadManager] Trying as version ID: " << mid << std::endl;
            std::string version_url = "https://civitai.com/api/v1/model-versions/" + std::to_string(mid);
            std::string response = http_get(version_url, 30);
            version = nlohmann::json::parse(response);

            // Version endpoint returns modelId and model info
            vid = mid;  // The input was actually a version ID
            mid = version.value("modelId", 0);
            if (mid == 0) {
                std::cerr << "[DownloadManager] Version response has no modelId" << std::endl;
                return std::nullopt;
            }

            // Get model name from version response if available
            if (version.contains("model") && version["model"].contains("name")) {
                model_name = version["model"]["name"].get<std::string>();
            } else {
                model_name = version.value("name", "Unknown Model");
            }
            if (version.contains("model") && version["model"].contains("type")) {
                model_type = version["model"]["type"].get<std::string>();
            }
        }

        if (version.is_null()) {
            std::cerr << "[DownloadManager] No valid version found" << std::endl;
            return std::nullopt;
        }

        CivitAIModelInfo info;
        info.model_id = mid;
        info.name = model_name;
        info.type = model_type;
        info.full_response = model_fetch_success ? json : version;
        info.version_id = version.value("id", 0);
        info.version_name = version.value("name", "");
        info.base_model = version.value("baseModel", "");

        // Get primary file (prefer safetensors)
        if (version.contains("files") && !version["files"].empty()) {
            auto& files = version["files"];
            nlohmann::json* selected_file = nullptr;

            // First pass: look for safetensors
            for (auto& file : files) {
                std::string fname = file.value("name", "");
                if (fname.find(".safetensors") != std::string::npos) {
                    selected_file = &file;
                    break;
                }
            }
            // Fallback: first file
            if (!selected_file) {
                selected_file = &files[0];
            }

            info.download_url = selected_file->value("downloadUrl", "");
            info.filename = selected_file->value("name", "");
            info.file_size = selected_file->value("sizeKB", 0.0) * 1024;

            if (selected_file->contains("hashes") && (*selected_file)["hashes"].contains("SHA256")) {
                info.sha256 = (*selected_file)["hashes"]["SHA256"].get<std::string>();
            }
        }

        return info;

    } catch (const std::exception& e) {
        std::cerr << "[DownloadManager] CivitAI API error: " << e.what() << std::endl;
        return std::nullopt;
    }
}

DownloadResult DownloadManager::download_from_civitai(
    const std::string& model_id,
    const std::string& model_type,
    const std::string& subfolder,
    DownloadProgressCallback progress_callback
) {
    DownloadResult result;

    try {
        // Get model info
        auto info = get_civitai_info(model_id);
        if (!info) {
            result.error_message = "Failed to get model info from CivitAI for ID: " + model_id;
            return result;
        }

        if (info->download_url.empty()) {
            result.error_message = "No download URL found for CivitAI model: " + model_id;
            return result;
        }

        // Get base directory for model type
        std::string base_dir = get_model_directory(model_type);

        // Add subfolder if specified
        std::string dest_dir = base_dir;
        if (!subfolder.empty()) {
            dest_dir = (fs::path(base_dir) / subfolder).string();
        }

        // Ensure directory exists
        if (!ensure_directory(dest_dir)) {
            result.error_message = "Failed to create directory: " + dest_dir;
            return result;
        }

        // Download file
        result = download_file(info->download_url, dest_dir, info->filename, progress_callback);

        if (result.success) {
            result.metadata = {
                {"source", "civitai"},
                {"model_id", info->model_id},
                {"version_id", info->version_id},
                {"name", info->name},
                {"version_name", info->version_name},
                {"base_model", info->base_model},
                {"type", info->type},
                {"expected_sha256", info->sha256},
                {"model_type", model_type},
                {"subfolder", subfolder}
            };
        }

    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    return result;
}

std::optional<HuggingFaceModelInfo> DownloadManager::get_huggingface_info(
    const std::string& repo_id,
    const std::string& filename,
    const std::string& revision
) {
    try {
        // Fetch file info from HuggingFace API
        std::string api_url = "https://huggingface.co/api/models/" + repo_id + "/tree/" + revision;
        std::string response = http_get(api_url, 30);

        auto json = nlohmann::json::parse(response);

        HuggingFaceModelInfo info;
        info.repo_id = repo_id;
        info.filename = filename;
        info.revision = revision;
        info.full_response = json;

        // Find the file in the tree
        for (const auto& item : json) {
            if (item.value("path", "") == filename) {
                info.file_size = item.value("size", 0);
                break;
            }
        }

        // Construct download URL
        info.download_url = "https://huggingface.co/" + repo_id + "/resolve/" + revision + "/" + filename;

        return info;

    } catch (const std::exception& e) {
        std::cerr << "[DownloadManager] HuggingFace API error: " << e.what() << std::endl;
        return std::nullopt;
    }
}

DownloadResult DownloadManager::download_from_huggingface(
    const std::string& repo_id,
    const std::string& filename,
    const std::string& model_type,
    const std::string& subfolder,
    const std::string& revision,
    DownloadProgressCallback progress_callback
) {
    DownloadResult result;

    try {
        // Validate filename extension
        fs::path file_path(filename);
        if (!is_supported_extension(file_path.extension().string())) {
            result.error_message = "Unsupported file extension: " + file_path.extension().string();
            return result;
        }

        // Get base directory for model type
        std::string base_dir = get_model_directory(model_type);

        // Add subfolder if specified
        std::string dest_dir = base_dir;
        if (!subfolder.empty()) {
            dest_dir = (fs::path(base_dir) / subfolder).string();
        }

        // Ensure directory exists
        if (!ensure_directory(dest_dir)) {
            result.error_message = "Failed to create directory: " + dest_dir;
            return result;
        }

        // Construct download URL
        std::string download_url = "https://huggingface.co/" + repo_id + "/resolve/" + revision + "/" + filename;

        // Download file
        result = download_file(download_url, dest_dir, filename, progress_callback);

        if (result.success) {
            result.metadata = {
                {"source", "huggingface"},
                {"repo_id", repo_id},
                {"filename", filename},
                {"revision", revision},
                {"model_type", model_type},
                {"subfolder", subfolder}
            };
        }

    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    return result;
}

} // namespace sdcpp
