#pragma once

#include "httplib.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>

#include "config.hpp"
#include "assistant_client.hpp"
#include "architecture_manager.hpp"
#include "settings_manager.hpp"

namespace sdcpp {

// Forward declarations
class ModelManager;
class QueueManager;

/**
 * Request Handlers - implements HTTP API endpoints
 */
class RequestHandlers {
public:
    /**
     * Constructor
     * @param model_manager Reference to model manager
     * @param queue_manager Reference to queue manager
     * @param output_dir Output directory path for file browser
     * @param webui_dir Optional webui directory path for serving web UI
     * @param ws_port WebSocket server port (0 if disabled)
     * @param assistant_config Assistant configuration for LLM helper
     * @param config_file_path Path to config.json for persisting settings (optional)
     */
    RequestHandlers(ModelManager& model_manager, QueueManager& queue_manager,
                    const std::string& output_dir, const std::string& webui_dir = "",
                    int ws_port = 0,
                    const AssistantConfig& assistant_config = AssistantConfig{},
                    const std::string& config_file_path = "");

    /**
     * Register all routes with the HTTP server
     * @param server HTTP server instance
     */
    void register_routes(httplib::Server& server);

private:
    // Model endpoints
    void handle_get_models(const httplib::Request& req, httplib::Response& res);
    void handle_refresh_models(const httplib::Request& req, httplib::Response& res);
    void handle_load_model(const httplib::Request& req, httplib::Response& res);
    void handle_unload_model(const httplib::Request& req, httplib::Response& res);
    void handle_get_model_hash(const httplib::Request& req, httplib::Response& res);

    // Generation endpoints
    void handle_txt2img(const httplib::Request& req, httplib::Response& res);
    void handle_img2img(const httplib::Request& req, httplib::Response& res);
    void handle_txt2vid(const httplib::Request& req, httplib::Response& res);
    void handle_upscale(const httplib::Request& req, httplib::Response& res);
    void handle_convert(const httplib::Request& req, httplib::Response& res);

    // Upscaler endpoints
    void handle_load_upscaler(const httplib::Request& req, httplib::Response& res);
    void handle_unload_upscaler(const httplib::Request& req, httplib::Response& res);

    // Queue endpoints
    void handle_get_queue(const httplib::Request& req, httplib::Response& res);
    void handle_get_job(const httplib::Request& req, httplib::Response& res);
    void handle_cancel_job(const httplib::Request& req, httplib::Response& res);
    void handle_delete_jobs(const httplib::Request& req, httplib::Response& res);

    // Health endpoint
    void handle_health(const httplib::Request& req, httplib::Response& res);

    // Options endpoint (samplers, schedulers)
    void handle_get_options(const httplib::Request& req, httplib::Response& res);

    // File browser endpoint
    void handle_file_browser(const httplib::Request& req, httplib::Response& res);
    void handle_thumbnail(const httplib::Request& req, httplib::Response& res);

    // Web UI endpoint
    void handle_webui(const httplib::Request& req, httplib::Response& res);

    // Preview settings endpoints
    void handle_get_preview_settings(const httplib::Request& req, httplib::Response& res);
    void handle_update_preview_settings(const httplib::Request& req, httplib::Response& res);

    // Assistant endpoints
    void handle_assistant_chat(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_history(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_clear_history(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_status(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_get_settings(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_update_settings(const httplib::Request& req, httplib::Response& res);

    // Architecture endpoints
    void handle_get_architectures(const httplib::Request& req, httplib::Response& res);

    // Download endpoints
    void handle_download_model(const httplib::Request& req, httplib::Response& res);
    void handle_get_civitai_info(const httplib::Request& req, httplib::Response& res);
    void handle_get_huggingface_info(const httplib::Request& req, httplib::Response& res);
    void handle_get_model_paths(const httplib::Request& req, httplib::Response& res);

    // Settings endpoints
    void handle_get_all_settings(const httplib::Request& req, httplib::Response& res);
    void handle_update_all_settings(const httplib::Request& req, httplib::Response& res);
    void handle_get_generation_defaults(const httplib::Request& req, httplib::Response& res);
    void handle_update_generation_defaults(const httplib::Request& req, httplib::Response& res);
    void handle_get_generation_defaults_for_mode(const httplib::Request& req, httplib::Response& res);
    void handle_update_generation_defaults_for_mode(const httplib::Request& req, httplib::Response& res);
    void handle_get_ui_preferences(const httplib::Request& req, httplib::Response& res);
    void handle_update_ui_preferences(const httplib::Request& req, httplib::Response& res);
    void handle_reset_settings(const httplib::Request& req, httplib::Response& res);

    // Thumbnail generation
    bool generate_thumbnail(const std::string& source_path, const std::string& thumb_path, int size = 120);
    std::string get_thumbnail_path(const std::string& source_path);
    bool is_image_file(const std::string& path);
    bool is_video_file(const std::string& path);

    // Utility methods
    void send_json(httplib::Response& res, const nlohmann::json& json, int status = 200);
    void send_error(httplib::Response& res, const std::string& message, int status = 400);
    nlohmann::json parse_json_body(const httplib::Request& req);
    std::string get_mime_type(const std::string& filepath);
    std::string format_file_size(size_t size);
    size_t calculate_directory_size(const std::string& path);
    std::time_t get_directory_content_mtime(const std::string& path);
    std::string generate_directory_html(const std::string& path, const std::string& url_path,
                                        const std::string& sort_by = "name", bool sort_asc = true);

    ModelManager& model_manager_;
    QueueManager& queue_manager_;
    std::string output_dir_;
    std::string webui_dir_;
    int ws_port_;
    std::unique_ptr<AssistantClient> assistant_client_;
    std::unique_ptr<ArchitectureManager> architecture_manager_;
    std::unique_ptr<SettingsManager> settings_manager_;
};

} // namespace sdcpp
