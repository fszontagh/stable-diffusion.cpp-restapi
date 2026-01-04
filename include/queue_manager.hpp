#pragma once

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <optional>

#include <nlohmann/json.hpp>
#include "sd_wrapper.hpp"

namespace sdcpp {

// Forward declarations
class ModelManager;

/**
 * Queue item status
 */
enum class QueueStatus {
    Pending,
    Processing,
    Completed,
    Failed,
    Cancelled
};

/**
 * Generation type
 */
enum class GenerationType {
    Text2Image,
    Image2Image,
    Text2Video,
    Upscale,
    Convert,
    ModelDownload,
    ModelHash
};

/**
 * Progress information - raw values from sd.cpp callback
 */
struct ProgressInfo {
    int step = 0;          // Current step (raw from sd.cpp)
    int total_steps = 0;   // Total steps (raw from sd.cpp)

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProgressInfo, step, total_steps)
};

/**
 * Queue item representing a generation job
 */
struct QueueItem {
    std::string job_id;
    GenerationType type;
    QueueStatus status = QueueStatus::Pending;
    nlohmann::json params;          // Generation parameters
    nlohmann::json model_settings;  // Model settings at time of job creation
    ProgressInfo progress;

    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;

    std::string error_message;
    std::vector<std::string> outputs;   // Output file paths (relative to output dir)

    // Linked job ID (e.g., hash job linked to download job)
    std::string linked_job_id;

    nlohmann::json to_json() const;
    static QueueItem from_json(const nlohmann::json& j);
};

/**
 * Filter parameters for queue listing
 */
struct QueueFilter {
    std::optional<QueueStatus> status;      // Filter by status
    std::optional<std::string> search;      // Search in prompt/negative_prompt (case-insensitive)
    std::optional<GenerationType> type;     // Filter by generation type
    std::optional<std::string> architecture;  // Filter by model architecture (case-insensitive partial match)

    // Pagination
    size_t limit = 20;                      // Max items per page (default 20)
    size_t offset = 0;                      // Items to skip

    // Date-based pagination (alternative to offset)
    std::optional<int64_t> before_timestamp;  // Only items created before this timestamp
    std::optional<int64_t> after_timestamp;   // Only items created after this timestamp

    /**
     * Check if a queue item matches the filter criteria
     */
    bool matches(const QueueItem& item) const;

    /**
     * Check if the filter is empty (no filtering)
     */
    bool is_empty() const;
};

/**
 * Paginated result for queue listing
 */
struct QueuePageResult {
    std::vector<QueueItem> items;
    size_t total_count;           // Total items matching filter (before pagination)
    size_t filtered_count;        // Items returned in this page
    size_t offset;
    size_t limit;
    bool has_more;                // More items available
    std::optional<int64_t> oldest_timestamp;  // Timestamp of oldest item in result
    std::optional<int64_t> newest_timestamp;  // Timestamp of newest item in result
};

/**
 * Date group for grouped queue listing
 */
struct QueueDateGroup {
    std::string date;             // Date string (YYYY-MM-DD)
    std::string label;            // Human-readable label (e.g., "Today", "Yesterday", "Dec 21, 2025")
    int64_t timestamp;            // Start of day timestamp
    std::vector<QueueItem> items;
    size_t count;                 // Total items in this date group
};

/**
 * Grouped result for queue listing by date
 */
struct QueueGroupedResult {
    std::vector<QueueDateGroup> groups;
    size_t total_count;           // Total items matching filter
    size_t page;                  // Current page (1-based)
    size_t total_pages;           // Total number of pages
    size_t limit;                 // Items per page
    bool has_more;                // More pages available
    bool has_prev;                // Previous pages available
};

/**
 * Queue Manager - handles job queue with FIFO processing
 * Thread-safe, persists state to disk
 */
class QueueManager {
public:
    /**
     * Constructor
     * @param model_manager Reference to model manager
     * @param output_dir Output directory path
     * @param state_file Path to queue state file for persistence
     */
    QueueManager(
        ModelManager& model_manager,
        const std::string& output_dir,
        const std::string& state_file
    );
    
    ~QueueManager();
    
    // Non-copyable
    QueueManager(const QueueManager&) = delete;
    QueueManager& operator=(const QueueManager&) = delete;
    
    /**
     * Start the worker thread
     */
    void start();
    
    /**
     * Stop the worker thread (waits for current job to finish)
     */
    void stop();
    
    /**
     * Add a job to the queue
     * @param type Generation type
     * @param params Generation parameters as JSON
     * @return Job ID (UUID)
     */
    std::string add_job(GenerationType type, const nlohmann::json& params);

    /**
     * Add a model download job with automatic hash job
     * Creates both download and hash jobs, with hash linked to download
     * @param params Download parameters (url, model_type, subfolder, source, etc.)
     * @return Pair of job IDs (download_job_id, hash_job_id)
     */
    std::pair<std::string, std::string> add_download_job(const nlohmann::json& params);

    /**
     * Fail a linked job (e.g., fail hash job if download fails)
     * @param job_id Job ID to fail
     * @param error_message Error message
     */
    void fail_linked_job(const std::string& job_id, const std::string& error_message);
    
    /**
     * Get a specific job by ID
     * @param job_id Job UUID
     * @return QueueItem if found
     */
    std::optional<QueueItem> get_job(const std::string& job_id) const;
    
    /**
     * Get all jobs
     * @return Vector of all queue items
     */
    std::vector<QueueItem> get_all_jobs() const;

    /**
     * Get all jobs with filtering
     * @param filter Filter parameters
     * @return Vector of matching queue items
     */
    std::vector<QueueItem> get_all_jobs(const QueueFilter& filter) const;

    /**
     * Get jobs with filtering and pagination
     * @param filter Filter parameters including pagination
     * @return Paginated result with items and metadata
     */
    QueuePageResult get_jobs_paginated(const QueueFilter& filter) const;

    /**
     * Get jobs grouped by date with filtering and pagination
     * @param filter Filter parameters
     * @param page Page number (1-based)
     * @param limit Items per page
     * @return Grouped result with date groups and items
     */
    QueueGroupedResult get_jobs_grouped_by_date(const QueueFilter& filter, size_t page = 1, size_t limit = 20) const;

    /**
     * Get queue status summary
     * @return JSON with pending_count, processing_count, etc.
     */
    nlohmann::json get_status() const;
    
    /**
     * Cancel a pending job
     * @param job_id Job UUID
     * @return true if job was cancelled
     */
    bool cancel_job(const std::string& job_id);
    
    /**
     * Delete a completed/failed/cancelled job from history
     * @param job_id Job UUID
     * @return true if job was deleted
     */
    bool delete_job(const std::string& job_id);
    
    /**
     * Clear all completed jobs from history
     */
    void clear_completed();
    
    /**
     * Get current progress of processing job
     */
    ProgressInfo get_current_progress() const;

    /**
     * Set preview mode for generation
     * @param mode Preview mode (None, Proj, Tae, Vae)
     * @param interval Generate preview every N steps
     * @param max_size Maximum preview dimension in pixels
     * @param quality JPEG quality 1-100
     */
    void set_preview_settings(PreviewMode mode, int interval = 1, int max_size = 256, int quality = 75);

    /**
     * Get current preview settings
     */
    struct PreviewSettings {
        PreviewMode mode = PreviewMode::Tae;
        int interval = 1;
        int max_size = 256;
        int quality = 75;
    };
    PreviewSettings get_preview_settings() const;

private:
    void worker_thread();
    void save_state();
    void load_state();
    void update_progress(int step, int total_steps);
    void set_batch_info(int total_images);
    void update_job_params(const std::string& job_id, const nlohmann::json& params);
    
    // Process job without holding queue_mutex_ (to avoid deadlock)
    std::vector<std::string> process_job_unlocked(
        GenerationType type, 
        const nlohmann::json& params, 
        const std::string& job_id
    );
    
    // Generate images/video based on job type (called without queue_mutex_)
    std::vector<std::string> process_txt2img_unlocked(const nlohmann::json& params, const std::string& job_id);
    std::vector<std::string> process_img2img_unlocked(const nlohmann::json& params, const std::string& job_id);
    std::vector<std::string> process_txt2vid_unlocked(const nlohmann::json& params, const std::string& job_id);
    std::vector<std::string> process_upscale_unlocked(const nlohmann::json& params, const std::string& job_id);
    std::vector<std::string> process_convert_unlocked(const nlohmann::json& params, const std::string& job_id);
    std::vector<std::string> process_model_download_unlocked(const nlohmann::json& params, const std::string& job_id);
    std::vector<std::string> process_model_hash_unlocked(const nlohmann::json& params, const std::string& job_id);

    // Save job config to output folder
    void save_job_config(const std::string& job_id, GenerationType type, const nlohmann::json& params);
    
    ModelManager& model_manager_;
    std::string output_dir_;
    std::string state_file_;
    
    // Queue storage
    mutable std::mutex queue_mutex_;
    std::map<std::string, QueueItem> jobs_;
    std::queue<std::string> pending_queue_;
    
    // Worker thread
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::condition_variable queue_cv_;
    
    // Current job progress
    mutable std::mutex progress_mutex_;
    std::string current_job_id_;
    ProgressInfo current_progress_;

    // Preview settings and state
    mutable std::mutex preview_mutex_;
    PreviewSettings preview_settings_;
    std::chrono::steady_clock::time_point last_preview_broadcast_;
    static constexpr std::chrono::milliseconds PREVIEW_THROTTLE_MS{200};

    // Preview callback for broadcasting
    void update_preview(int step, int frame_count, const std::vector<uint8_t>& jpeg_data,
                       int width, int height, bool is_noisy);
};

// String conversions
std::string queue_status_to_string(QueueStatus status);
QueueStatus string_to_queue_status(const std::string& str);

std::string generation_type_to_string(GenerationType type);
GenerationType string_to_generation_type(const std::string& str);

} // namespace sdcpp
