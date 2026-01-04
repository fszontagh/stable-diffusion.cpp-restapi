#include "download_manager.hpp"
#include "httplib.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <chrono>

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

std::string DownloadManager::http_get(const std::string& url, int timeout_seconds) {
    // Parse URL
    std::regex url_regex("(https?)://([^/:]+)(:\\d+)?(/.*)?");
    std::smatch match;
    if (!std::regex_match(url, match, url_regex)) {
        throw std::runtime_error("Invalid URL: " + url);
    }

    std::string scheme = match[1].str();
    std::string host = match[2].str();
    std::string port_str = match[3].str();
    std::string path = match[4].str();
    if (path.empty()) path = "/";

    int port = (scheme == "https") ? 443 : 80;
    if (!port_str.empty()) {
        port = std::stoi(port_str.substr(1));
    }

    std::unique_ptr<httplib::Client> client;
    if (scheme == "https") {
        client = std::make_unique<httplib::Client>(host, port);
        client->enable_server_certificate_verification(false);
    } else {
        client = std::make_unique<httplib::Client>(host, port);
    }

    client->set_connection_timeout(timeout_seconds);
    client->set_read_timeout(timeout_seconds);
    client->set_follow_location(true);
    client->set_default_headers({
        {"User-Agent", "SDCpp-RestAPI/1.0"}
    });

    auto res = client->Get(path);
    if (!res) {
        throw std::runtime_error("HTTP request failed: " + httplib::to_string(res.error()));
    }

    if (res->status != 200) {
        throw std::runtime_error("HTTP " + std::to_string(res->status) + ": " + res->reason);
    }

    return res->body;
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

    // Parse URL
    std::regex url_regex("(https?)://([^/:]+)(:\\d+)?(/.*)?");
    std::smatch match;
    if (!std::regex_match(url, match, url_regex)) {
        result.error_message = "Invalid URL: " + url;
        return result;
    }

    std::string scheme = match[1].str();
    std::string host = match[2].str();
    std::string port_str = match[3].str();
    std::string path = match[4].str();
    if (path.empty()) path = "/";

    int port = (scheme == "https") ? 443 : 80;
    if (!port_str.empty()) {
        port = std::stoi(port_str.substr(1));
    }

    std::unique_ptr<httplib::Client> client;
    if (scheme == "https") {
        client = std::make_unique<httplib::Client>(host, port);
        client->enable_server_certificate_verification(false);
    } else {
        client = std::make_unique<httplib::Client>(host, port);
    }

    client->set_connection_timeout(30);
    client->set_read_timeout(300);  // 5 minutes for large files
    client->set_follow_location(true);
    client->set_default_headers({
        {"User-Agent", "SDCpp-RestAPI/1.0"}
    });

    // Track download progress
    auto start_time = std::chrono::steady_clock::now();

    // First, do a HEAD request to get content info
    auto head_res = client->Head(path);
    size_t total_size = 0;
    std::string content_type;
    std::string content_disposition;

    if (head_res && head_res->status == 200) {
        if (head_res->has_header("Content-Length")) {
            total_size = std::stoull(head_res->get_header_value("Content-Length"));
        }
        if (head_res->has_header("Content-Type")) {
            content_type = head_res->get_header_value("Content-Type");
        }
        if (head_res->has_header("Content-Disposition")) {
            content_disposition = head_res->get_header_value("Content-Disposition");
        }
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

    // Download with progress
    std::ofstream ofs(full_path, std::ios::binary);
    if (!ofs) {
        result.error_message = "Failed to create file: " + full_path;
        return result;
    }

    size_t downloaded = 0;

    auto res = client->Get(path,
        [&](const char* data, size_t data_length) {
            ofs.write(data, data_length);
            downloaded += data_length;

            if (progress_callback) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                size_t speed = elapsed > 0 ? downloaded / elapsed : downloaded;
                progress_callback(downloaded, total_size, speed);
            }

            return true;  // Continue download
        });

    ofs.close();

    if (!res) {
        fs::remove(full_path);  // Clean up partial file
        result.error_message = "Download failed: " + httplib::to_string(res.error());
        return result;
    }

    if (res->status != 200) {
        fs::remove(full_path);  // Clean up partial file
        result.error_message = "Download failed with HTTP " + std::to_string(res->status);
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
