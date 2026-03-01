#include "queue_manager.hpp"
#include "queue_item_fields.hpp"
#include "model_manager.hpp"
#include "download_manager.hpp"
#include "sd_wrapper.hpp"
#include "websocket_server.hpp"
#include "utils.hpp"
#include "config.hpp"

// Alias for shorter code
using F = sdcpp::QueueItemFields;

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace sdcpp {

std::string queue_status_to_string(QueueStatus status) {
    switch (status) {
        case QueueStatus::Pending: return "pending";
        case QueueStatus::Processing: return "processing";
        case QueueStatus::Completed: return "completed";
        case QueueStatus::Failed: return "failed";
        case QueueStatus::Cancelled: return "cancelled";
        case QueueStatus::Deleted: return "deleted";
        default: return "unknown";
    }
}

QueueStatus string_to_queue_status(const std::string& str) {
    if (str == "pending") return QueueStatus::Pending;
    if (str == "processing") return QueueStatus::Processing;
    if (str == "completed") return QueueStatus::Completed;
    if (str == "failed") return QueueStatus::Failed;
    if (str == "cancelled") return QueueStatus::Cancelled;
    if (str == "deleted") return QueueStatus::Deleted;
    return QueueStatus::Pending;
}

std::string generation_type_to_string(GenerationType type) {
    switch (type) {
        case GenerationType::Text2Image: return "txt2img";
        case GenerationType::Image2Image: return "img2img";
        case GenerationType::Text2Video: return "txt2vid";
        case GenerationType::Upscale: return "upscale";
        case GenerationType::Convert: return "convert";
        case GenerationType::ModelDownload: return "model_download";
        case GenerationType::ModelHash: return "model_hash";
        default: return "unknown";
    }
}

GenerationType string_to_generation_type(const std::string& str) {
    if (str == "txt2img") return GenerationType::Text2Image;
    if (str == "img2img") return GenerationType::Image2Image;
    if (str == "txt2vid") return GenerationType::Text2Video;
    if (str == "upscale") return GenerationType::Upscale;
    if (str == "convert") return GenerationType::Convert;
    if (str == "model_download") return GenerationType::ModelDownload;
    if (str == "model_hash") return GenerationType::ModelHash;
    return GenerationType::Text2Image;
}

nlohmann::json QueueItem::to_json() const {
    nlohmann::json j = {
        {F::JOB_ID, job_id},
        {F::TYPE, generation_type_to_string(type)},
        {F::STATUS, queue_status_to_string(status)},
        {F::PROGRESS, progress},  // Uses NLOHMANN_DEFINE_TYPE_INTRUSIVE
        {F::CREATED_AT, utils::time_to_string(created_at)},
        {F::OUTPUTS, outputs}
    };

    if (status == QueueStatus::Processing || status == QueueStatus::Completed) {
        j[F::STARTED_AT] = utils::time_to_string(started_at);
    }
    if (status == QueueStatus::Completed || status == QueueStatus::Failed) {
        j[F::COMPLETED_AT] = utils::time_to_string(completed_at);
    }
    if (status == QueueStatus::Failed && !error_message.empty()) {
        j[F::ERROR] = error_message;
    }
    if (!params.empty()) {
        j[F::PARAMS] = params;
    }
    if (!model_settings.empty()) {
        j[F::MODEL_SETTINGS] = model_settings;
    }
    if (!linked_job_id.empty()) {
        j[F::LINKED_JOB_ID] = linked_job_id;
    }

    // Recycle bin fields
    if (status == QueueStatus::Deleted) {
        j["deleted_at"] = utils::time_to_string(deleted_at);
        j["previous_status"] = queue_status_to_string(previous_status);
    }

    return j;
}

QueueItem QueueItem::from_json(const nlohmann::json& j) {
    QueueItem item;
    item.job_id = j.value(F::JOB_ID, "");
    item.type = string_to_generation_type(j.value(F::TYPE, F::TYPE_TXT2IMG));
    item.status = string_to_queue_status(j.value(F::STATUS, F::STATUS_PENDING));

    if (j.contains(F::CREATED_AT)) {
        item.created_at = utils::string_to_time(j[F::CREATED_AT].get<std::string>());
    }
    if (j.contains(F::STARTED_AT)) {
        item.started_at = utils::string_to_time(j[F::STARTED_AT].get<std::string>());
    }
    if (j.contains(F::COMPLETED_AT)) {
        item.completed_at = utils::string_to_time(j[F::COMPLETED_AT].get<std::string>());
    }
    if (j.contains(F::OUTPUTS)) {
        item.outputs = j[F::OUTPUTS].get<std::vector<std::string>>();
    }
    if (j.contains(F::PARAMS)) {
        item.params = j[F::PARAMS];
    }
    if (j.contains(F::MODEL_SETTINGS)) {
        item.model_settings = j[F::MODEL_SETTINGS];
    }
    if (j.contains(F::ERROR)) {
        item.error_message = j[F::ERROR].get<std::string>();
    }
    if (j.contains(F::LINKED_JOB_ID)) {
        item.linked_job_id = j[F::LINKED_JOB_ID].get<std::string>();
    }

    // Recycle bin fields
    if (j.contains("deleted_at")) {
        item.deleted_at = utils::string_to_time(j["deleted_at"].get<std::string>());
    }
    if (j.contains("previous_status")) {
        item.previous_status = string_to_queue_status(j["previous_status"].get<std::string>());
    }

    return item;
}

// Helper for case-insensitive substring search
static bool contains_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.empty()) return false;

    std::string h = haystack;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    return h.find(n) != std::string::npos;
}

bool QueueFilter::matches(const QueueItem& item) const {
    // Exclude deleted items unless explicitly filtering for deleted status
    if (item.status == QueueStatus::Deleted &&
        (!status.has_value() || status.value() != QueueStatus::Deleted)) {
        return false;
    }

    // Status filter
    if (status.has_value() && item.status != status.value()) {
        return false;
    }

    // Type filter
    if (type.has_value() && item.type != type.value()) {
        return false;
    }

    // Search filter (search in prompt and negative_prompt)
    if (search.has_value() && !search.value().empty()) {
        const std::string& query = search.value();
        std::string prompt;
        std::string neg_prompt;
        std::string job_id = item.job_id;

        if (item.params.contains("prompt") && item.params["prompt"].is_string()) {
            prompt = item.params["prompt"].get<std::string>();
        }
        if (item.params.contains("negative_prompt") && item.params["negative_prompt"].is_string()) {
            neg_prompt = item.params["negative_prompt"].get<std::string>();
        }

        // Check if query matches any of the searchable fields
        if (!contains_insensitive(prompt, query) &&
            !contains_insensitive(neg_prompt, query) &&
            !contains_insensitive(job_id, query)) {
            return false;
        }
    }

    // Architecture filter (search in model_settings.model_architecture)
    if (architecture.has_value() && !architecture.value().empty()) {
        std::string item_arch;
        if (item.model_settings.contains("model_architecture") &&
            item.model_settings["model_architecture"].is_string()) {
            item_arch = item.model_settings["model_architecture"].get<std::string>();
        }
        if (!contains_insensitive(item_arch, architecture.value())) {
            return false;
        }
    }

    // Model name filter (search in model_settings.model_name)
    if (model.has_value() && !model.value().empty()) {
        std::string item_model;
        if (item.model_settings.contains("model_name") &&
            item.model_settings["model_name"].is_string()) {
            item_model = item.model_settings["model_name"].get<std::string>();
        }
        if (!contains_insensitive(item_model, model.value())) {
            return false;
        }
    }

    // Date-based filters
    auto item_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        item.created_at.time_since_epoch()
    ).count();

    if (before_timestamp.has_value() && item_timestamp >= before_timestamp.value()) {
        return false;
    }

    if (after_timestamp.has_value() && item_timestamp <= after_timestamp.value()) {
        return false;
    }

    return true;
}

bool QueueFilter::is_empty() const {
    return !status.has_value() && !search.has_value() && !type.has_value() && !architecture.has_value() && !model.has_value();
}

QueueManager::QueueManager(
    ModelManager& model_manager,
    const std::string& output_dir,
    const std::string& state_file,
    const RecycleBinConfig& recycle_bin_config
) : model_manager_(model_manager),
    output_dir_(output_dir),
    state_file_(state_file),
    recycle_bin_config_(recycle_bin_config) {

    utils::create_directory(output_dir_);
    load_state();

    // Purge expired recycle bin items on startup
    if (recycle_bin_config_.enabled) {
        int purged = purge_expired_jobs();
        if (purged > 0) {
            std::cout << "[QueueManager] Purged " << purged << " expired items from recycle bin on startup" << std::endl;
        }
    }
}

QueueManager::~QueueManager() {
    stop();
}

void QueueManager::start() {
    if (running_) return;
    
    running_ = true;
    worker_ = std::thread(&QueueManager::worker_thread, this);
    std::cout << "[QueueManager] Worker thread started" << std::endl;
}

void QueueManager::stop() {
    if (!running_) return;

    running_ = false;
    queue_cv_.notify_all();

    if (worker_.joinable()) {
        // Use a timed approach: wait up to 5 seconds, then detach
        // SD.cpp doesn't support mid-generation cancellation, so we can't interrupt a running job
        std::cout << "[QueueManager] Waiting for worker thread to finish..." << std::endl;

        // Try to join with a timeout by polling
        auto start = std::chrono::steady_clock::now();
        constexpr auto SHUTDOWN_TIMEOUT = std::chrono::seconds(5);

        while (worker_.joinable()) {
            // Check if thread has finished using a native handle approach isn't portable,
            // so we just try join with a small sleep and check elapsed time
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= SHUTDOWN_TIMEOUT) {
                std::cout << "[QueueManager] Timeout waiting for worker, detaching thread" << std::endl;
                worker_.detach();
                break;
            }

            // Try a quick join check - there's no portable timed_join, so just sleep and check
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check if we can join by seeing if running is still active in the thread
            // Since we set running_ = false, if the thread is waiting on CV, it will exit
            // If it's in the middle of processing, it won't exit until done

            // Attempt join with a trick: check if thread is still doing work
            // by seeing if current_job_id_ is still set
            {
                std::lock_guard<std::mutex> plock(progress_mutex_);
                if (current_job_id_.empty()) {
                    // No job running, thread should exit soon
                    worker_.join();
                    break;
                }
            }
        }
    }

    save_state();
    std::cout << "[QueueManager] Worker thread stopped" << std::endl;
}

std::string QueueManager::add_job(GenerationType type, const nlohmann::json& params) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    QueueItem item;
    item.job_id = utils::generate_uuid();
    item.type = type;
    item.status = QueueStatus::Pending;
    item.params = params;
    item.created_at = utils::get_time_now();

    // Capture current model settings at job creation time
    item.model_settings = model_manager_.get_loaded_models_info();

    jobs_[item.job_id] = item;
    pending_queue_.push(item.job_id);

    save_state();
    queue_cv_.notify_one();

    // Broadcast job added event via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::JobAdded, {
            {"job_id", item.job_id},
            {"type", generation_type_to_string(type)},
            {"queue_position", pending_queue_.size()}
        });
    }

    // Log job creation with details
    std::string prompt_preview;
    if (params.contains("prompt")) {
        prompt_preview = params["prompt"].get<std::string>();
        if (prompt_preview.length() > 50) {
            prompt_preview = prompt_preview.substr(0, 47) + "...";
        }
    }
    std::cout << "[QueueManager] Job added: " << item.job_id
              << " | type=" << generation_type_to_string(type)
              << " | queue_size=" << pending_queue_.size();
    if (!prompt_preview.empty()) {
        std::cout << " | prompt=\"" << prompt_preview << "\"";
    }
    std::cout << std::endl;

    return item.job_id;
}

std::optional<QueueItem> QueueManager::get_job(const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        return std::nullopt;
    }
    
    QueueItem item = it->second;
    
    // Add current progress if this is the processing job
    if (item.status == QueueStatus::Processing && job_id == current_job_id_) {
        std::lock_guard<std::mutex> plock(progress_mutex_);
        item.progress = current_progress_;
    }
    
    return item;
}

std::vector<QueueItem> QueueManager::get_all_jobs() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    std::vector<QueueItem> result;
    for (const auto& [id, item] : jobs_) {
        QueueItem copy = item;
        if (copy.status == QueueStatus::Processing && id == current_job_id_) {
            std::lock_guard<std::mutex> plock(progress_mutex_);
            copy.progress = current_progress_;
        }
        result.push_back(copy);
    }
    return result;
}

std::vector<QueueItem> QueueManager::get_all_jobs(const QueueFilter& filter) const {
    // If no filter, use the faster unfiltered version
    if (filter.is_empty()) {
        return get_all_jobs();
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    std::vector<QueueItem> result;
    for (const auto& [id, item] : jobs_) {
        // Check if item matches filter
        if (!filter.matches(item)) {
            continue;
        }

        QueueItem copy = item;
        if (copy.status == QueueStatus::Processing && id == current_job_id_) {
            std::lock_guard<std::mutex> plock(progress_mutex_);
            copy.progress = current_progress_;
        }
        result.push_back(copy);
    }
    return result;
}

QueuePageResult QueueManager::get_jobs_paginated(const QueueFilter& filter) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    QueuePageResult result;
    result.offset = filter.offset;
    result.limit = filter.limit > 0 ? filter.limit : 20;

    // First, collect all matching items and sort by created_at (newest first)
    std::vector<QueueItem> matching_items;
    for (const auto& [id, item] : jobs_) {
        if (filter.matches(item)) {
            QueueItem copy = item;
            if (copy.status == QueueStatus::Processing && id == current_job_id_) {
                std::lock_guard<std::mutex> plock(progress_mutex_);
                copy.progress = current_progress_;
            }
            matching_items.push_back(copy);
        }
    }

    // Sort by created_at descending (newest first)
    std::sort(matching_items.begin(), matching_items.end(),
        [](const QueueItem& a, const QueueItem& b) {
            return a.created_at > b.created_at;
        });

    result.total_count = matching_items.size();

    // Apply pagination
    size_t start = std::min(filter.offset, matching_items.size());
    size_t end = std::min(start + result.limit, matching_items.size());

    for (size_t i = start; i < end; ++i) {
        result.items.push_back(matching_items[i]);
    }

    result.filtered_count = result.items.size();
    result.has_more = end < matching_items.size();

    // Set timestamp bounds for the returned items
    if (!result.items.empty()) {
        result.newest_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            result.items.front().created_at.time_since_epoch()
        ).count();
        result.oldest_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            result.items.back().created_at.time_since_epoch()
        ).count();
    }

    return result;
}

// Helper to get the start of day timestamp for a given time point
static int64_t get_start_of_day(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_info = *std::localtime(&time);
    tm_info.tm_hour = 0;
    tm_info.tm_min = 0;
    tm_info.tm_sec = 0;
    return std::mktime(&tm_info);
}

// Helper to format a date label (Today, Yesterday, or formatted date)
static std::string format_date_label(int64_t day_timestamp) {
    auto now = std::chrono::system_clock::now();
    int64_t today_start = get_start_of_day(now);
    int64_t yesterday_start = today_start - 86400;  // 24 * 60 * 60

    if (day_timestamp == today_start) {
        return "Today";
    } else if (day_timestamp == yesterday_start) {
        return "Yesterday";
    } else {
        // Format as "Dec 21, 2025"
        std::time_t time = static_cast<std::time_t>(day_timestamp);
        std::tm tm_info = *std::localtime(&time);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%b %d, %Y", &tm_info);
        return buffer;
    }
}

// Helper to format date string (YYYY-MM-DD)
static std::string format_date_string(int64_t day_timestamp) {
    std::time_t time = static_cast<std::time_t>(day_timestamp);
    std::tm tm_info = *std::localtime(&time);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm_info);
    return buffer;
}

QueueGroupedResult QueueManager::get_jobs_grouped_by_date(const QueueFilter& filter, size_t page, size_t limit) const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    QueueGroupedResult result;
    result.page = page > 0 ? page : 1;
    result.limit = limit > 0 ? limit : 20;

    // First, collect all matching items and sort by created_at (newest first)
    std::vector<QueueItem> matching_items;
    for (const auto& [id, item] : jobs_) {
        if (filter.matches(item)) {
            QueueItem copy = item;
            if (copy.status == QueueStatus::Processing && id == current_job_id_) {
                std::lock_guard<std::mutex> plock(progress_mutex_);
                copy.progress = current_progress_;
            }
            matching_items.push_back(copy);
        }
    }

    result.total_count = matching_items.size();

    // Sort by created_at descending (newest first)
    std::sort(matching_items.begin(), matching_items.end(),
        [](const QueueItem& a, const QueueItem& b) {
            return a.created_at > b.created_at;
        });

    // Group items by date
    std::map<int64_t, std::vector<QueueItem>, std::greater<int64_t>> date_groups;
    for (const auto& item : matching_items) {
        int64_t day_start = get_start_of_day(item.created_at);
        date_groups[day_start].push_back(item);
    }

    // Calculate pagination
    result.total_pages = (matching_items.size() + result.limit - 1) / result.limit;
    if (result.total_pages == 0) result.total_pages = 1;

    size_t skip = (result.page - 1) * result.limit;
    size_t taken = 0;
    size_t skipped = 0;

    // Iterate through date groups and apply pagination
    for (auto& [day_timestamp, items] : date_groups) {
        // If we've taken enough items, stop
        if (taken >= result.limit) break;

        // Check if we need to skip this entire group
        if (skipped + items.size() <= skip) {
            skipped += items.size();
            continue;
        }

        QueueDateGroup group;
        group.timestamp = day_timestamp;
        group.date = format_date_string(day_timestamp);
        group.label = format_date_label(day_timestamp);
        group.count = items.size();

        // Calculate how many items to skip within this group
        size_t group_skip = (skipped < skip) ? (skip - skipped) : 0;

        // Take items from this group
        for (size_t i = group_skip; i < items.size() && taken < result.limit; ++i) {
            group.items.push_back(items[i]);
            ++taken;
        }

        skipped += group_skip;

        if (!group.items.empty()) {
            result.groups.push_back(group);
        }
    }

    result.has_more = (result.page < result.total_pages);
    result.has_prev = (result.page > 1);

    return result;
}

nlohmann::json QueueManager::get_status() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    int pending = 0, processing = 0, completed = 0, failed = 0;
    for (const auto& [id, item] : jobs_) {
        switch (item.status) {
            case QueueStatus::Pending: pending++; break;
            case QueueStatus::Processing: processing++; break;
            case QueueStatus::Completed: completed++; break;
            case QueueStatus::Failed: failed++; break;
            default: break;
        }
    }
    
    return nlohmann::json{
        {"pending_count", pending},
        {"processing_count", processing},
        {"completed_count", completed},
        {"failed_count", failed},
        {"total_count", jobs_.size()}
    };
}

bool QueueManager::cancel_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        std::cout << "[QueueManager] Cancel failed: job " << job_id << " not found" << std::endl;
        return false;
    }

    if (it->second.status != QueueStatus::Pending) {
        std::cout << "[QueueManager] Cancel failed: job " << job_id
                  << " is " << queue_status_to_string(it->second.status)
                  << " (only pending jobs can be cancelled)" << std::endl;
        return false;
    }

    it->second.status = QueueStatus::Cancelled;
    save_state();

    // Broadcast job cancelled event via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::JobCancelled, {
            {"job_id", job_id}
        });
    }

    std::cout << "[QueueManager] Job cancelled: " << job_id
              << " | type=" << generation_type_to_string(it->second.type) << std::endl;
    return true;
}

bool QueueManager::delete_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        std::cout << "[QueueManager] Delete failed: job " << job_id << " not found" << std::endl;
        return false;
    }

    if (it->second.status == QueueStatus::Processing) {
        std::cout << "[QueueManager] Delete failed: job " << job_id
                  << " is currently processing" << std::endl;
        return false;
    }

    // Already deleted - nothing to do
    if (it->second.status == QueueStatus::Deleted) {
        std::cout << "[QueueManager] Delete failed: job " << job_id
                  << " is already in recycle bin" << std::endl;
        return false;
    }

    std::string type = generation_type_to_string(it->second.type);
    std::string status = queue_status_to_string(it->second.status);

    if (recycle_bin_config_.enabled) {
        // Soft delete - move to recycle bin
        it->second.previous_status = it->second.status;
        it->second.status = QueueStatus::Deleted;
        it->second.deleted_at = std::chrono::system_clock::now();
        save_state();

        // Broadcast job deleted event via WebSocket
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::JobDeleted, {
                {"job_id", job_id},
                {"soft_delete", true}
            });
        }

        std::cout << "[QueueManager] Job moved to recycle bin: " << job_id
                  << " | type=" << type << " | was_status=" << status << std::endl;
    } else {
        // Hard delete - permanent removal
        jobs_.erase(it);
        save_state();

        // Broadcast job deleted event via WebSocket
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::JobDeleted, {
                {"job_id", job_id},
                {"soft_delete", false}
            });
        }

        std::cout << "[QueueManager] Job permanently deleted: " << job_id
                  << " | type=" << type << " | was_status=" << status << std::endl;
    }

    return true;
}

void QueueManager::clear_completed() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    int cleared = 0;
    auto now = std::chrono::system_clock::now();

    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->second.status == QueueStatus::Completed ||
            it->second.status == QueueStatus::Failed ||
            it->second.status == QueueStatus::Cancelled) {

            if (recycle_bin_config_.enabled) {
                // Soft delete - move to recycle bin
                it->second.previous_status = it->second.status;
                it->second.status = QueueStatus::Deleted;
                it->second.deleted_at = now;
                ++it;
            } else {
                // Hard delete
                it = jobs_.erase(it);
            }
            cleared++;
        } else {
            ++it;
        }
    }
    save_state();

    if (recycle_bin_config_.enabled) {
        std::cout << "[QueueManager] Moved " << cleared << " completed/failed/cancelled jobs to recycle bin" << std::endl;
    } else {
        std::cout << "[QueueManager] Cleared " << cleared << " completed/failed/cancelled jobs" << std::endl;
    }
}

bool QueueManager::restore_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        std::cout << "[QueueManager] Restore failed: job " << job_id << " not found" << std::endl;
        return false;
    }

    if (it->second.status != QueueStatus::Deleted) {
        std::cout << "[QueueManager] Restore failed: job " << job_id
                  << " is not in recycle bin (status=" << queue_status_to_string(it->second.status) << ")" << std::endl;
        return false;
    }

    // Restore to previous status
    it->second.status = it->second.previous_status;
    it->second.deleted_at = std::chrono::system_clock::time_point{};  // Reset
    it->second.previous_status = QueueStatus::Pending;  // Reset
    save_state();

    // Broadcast job restored event via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::JobRestored, {
            {"job_id", job_id},
            {"status", queue_status_to_string(it->second.status)}
        });
    }

    std::cout << "[QueueManager] Job restored from recycle bin: " << job_id
              << " | status=" << queue_status_to_string(it->second.status) << std::endl;
    return true;
}

bool QueueManager::purge_job(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        std::cout << "[QueueManager] Purge failed: job " << job_id << " not found" << std::endl;
        return false;
    }

    if (it->second.status == QueueStatus::Processing) {
        std::cout << "[QueueManager] Purge failed: job " << job_id
                  << " is currently processing" << std::endl;
        return false;
    }

    std::string type = generation_type_to_string(it->second.type);
    std::string status = queue_status_to_string(it->second.status);
    jobs_.erase(it);
    save_state();

    // Broadcast job deleted event via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::JobDeleted, {
            {"job_id", job_id},
            {"soft_delete", false}
        });
    }

    std::cout << "[QueueManager] Job permanently purged: " << job_id
              << " | type=" << type << " | was_status=" << status << std::endl;
    return true;
}

std::vector<QueueItem> QueueManager::get_deleted_jobs() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    std::vector<QueueItem> deleted_jobs;
    for (const auto& [id, item] : jobs_) {
        if (item.status == QueueStatus::Deleted) {
            deleted_jobs.push_back(item);
        }
    }

    // Sort by deleted_at descending (most recent first)
    std::sort(deleted_jobs.begin(), deleted_jobs.end(),
              [](const QueueItem& a, const QueueItem& b) {
                  return a.deleted_at > b.deleted_at;
              });

    return deleted_jobs;
}

int QueueManager::purge_expired_jobs() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto now = std::chrono::system_clock::now();
    auto retention = std::chrono::minutes(recycle_bin_config_.retention_minutes);
    int purged = 0;

    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->second.status == QueueStatus::Deleted) {
            auto age = now - it->second.deleted_at;
            if (age > retention) {
                std::cout << "[QueueManager] Purging expired job: " << it->first << std::endl;
                it = jobs_.erase(it);
                purged++;
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }

    if (purged > 0) {
        save_state();
    }

    return purged;
}

int QueueManager::clear_recycle_bin() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    int purged = 0;
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->second.status == QueueStatus::Deleted) {
            it = jobs_.erase(it);
            purged++;
        } else {
            ++it;
        }
    }

    if (purged > 0) {
        save_state();
    }

    std::cout << "[QueueManager] Cleared recycle bin: " << purged << " jobs purged" << std::endl;
    return purged;
}

ProgressInfo QueueManager::get_current_progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return current_progress_;
}

void QueueManager::worker_thread() {
    while (running_) {
        std::string job_id;
        GenerationType job_type = GenerationType::Text2Image;
        nlohmann::json job_params;
        std::chrono::system_clock::time_point job_start_time;

        // Step 1: Get next job (with lock)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !pending_queue_.empty();
            });

            if (!running_) break;

            // Find next non-cancelled job
            while (!pending_queue_.empty()) {
                job_id = pending_queue_.front();
                pending_queue_.pop();

                auto it = jobs_.find(job_id);
                if (it != jobs_.end() && it->second.status == QueueStatus::Pending) {
                    it->second.status = QueueStatus::Processing;
                    it->second.started_at = utils::get_time_now();
                    job_start_time = it->second.started_at;
                    // Copy data we need for processing
                    job_type = it->second.type;
                    job_params = it->second.params;

                    // Broadcast status change via WebSocket
                    if (auto* ws = get_websocket_server()) {
                        ws->broadcast(WSEventType::JobStatusChanged, {
                            {"job_id", job_id},
                            {"status", "processing"},
                            {"previous_status", "pending"}
                        });
                    }

                    std::cout << "[QueueManager] Job status: " << job_id
                              << " | pending -> processing"
                              << " | type=" << generation_type_to_string(job_type)
                              << " | remaining_in_queue=" << pending_queue_.size() << std::endl;
                    break;
                }
                job_id.clear();
            }

            if (job_id.empty()) continue;
        }
        // Lock released here

        // Step 2: Set progress tracking (with progress lock only)
        {
            std::lock_guard<std::mutex> plock(progress_mutex_);
            current_job_id_ = job_id;
            current_progress_ = ProgressInfo{};
        }

        // Step 3: Process job WITHOUT holding queue_mutex_
        std::vector<std::string> outputs;
        std::string error_message;
        bool success = false;

        try {
            outputs = process_job_unlocked(job_type, job_params, job_id);
            success = true;
        } catch (const std::exception& e) {
            error_message = e.what();
            std::cerr << "[QueueManager] Job error: " << job_id << " | " << e.what() << std::endl;
        }

        // Calculate duration
        auto job_end_time = utils::get_time_now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(job_end_time - job_start_time).count();
        float duration_sec = duration / 1000.0f;

        // Step 4: Save final progress and update job status
        {
            // First, capture the final progress
            ProgressInfo final_progress;
            {
                std::lock_guard<std::mutex> plock(progress_mutex_);
                final_progress = current_progress_;
            }

            // Then update the job with final progress and status
            std::lock_guard<std::mutex> lock(queue_mutex_);
            auto it = jobs_.find(job_id);
            if (it != jobs_.end()) {
                // Save final progress to job record
                it->second.progress = final_progress;

                if (success) {
                    it->second.status = QueueStatus::Completed;
                    it->second.outputs = outputs;

                    // Broadcast job completed via WebSocket
                    if (auto* ws = get_websocket_server()) {
                        ws->broadcast(WSEventType::JobStatusChanged, {
                            {"job_id", job_id},
                            {"status", "completed"},
                            {"previous_status", "processing"},
                            {"outputs", outputs}
                        });
                    }

                    std::cout << "[QueueManager] Job status: " << job_id
                              << " | processing -> completed"
                              << " | duration=" << std::fixed << std::setprecision(1) << duration_sec << "s"
                              << " | outputs=" << outputs.size() << std::endl;
                } else {
                    it->second.status = QueueStatus::Failed;
                    it->second.error_message = error_message;

                    // Broadcast job failed via WebSocket
                    if (auto* ws = get_websocket_server()) {
                        ws->broadcast(WSEventType::JobStatusChanged, {
                            {"job_id", job_id},
                            {"status", "failed"},
                            {"previous_status", "processing"},
                            {"error", error_message}
                        });
                    }

                    std::cout << "[QueueManager] Job status: " << job_id
                              << " | processing -> failed"
                              << " | duration=" << std::fixed << std::setprecision(1) << duration_sec << "s"
                              << " | error=\"" << error_message << "\"" << std::endl;
                }
                it->second.completed_at = job_end_time;
            }
        }

        // Step 5: Clear progress tracking and preview buffer
        {
            std::lock_guard<std::mutex> plock(progress_mutex_);
            current_job_id_.clear();
        }
        clear_preview_buffer(job_id);

        save_state();
    }
}

std::vector<std::string> QueueManager::process_job_unlocked(
    GenerationType type,
    const nlohmann::json& params,
    const std::string& job_id
) {
    // Get expected diffusion steps from params (for phase detection in progress callback)
    int expected_steps = 0;
    if (type == GenerationType::Text2Image || type == GenerationType::Image2Image) {
        expected_steps = params.value("steps", 20);
    } else if (type == GenerationType::Text2Video) {
        expected_steps = params.value("steps", 30);
    }

    // Set up progress callback with expected steps for phase detection
    SDWrapper::set_progress_callback([this](int step, int total) {
        update_progress(step, total);
    }, expected_steps);

    // Set up preview callback with current settings
    PreviewSettings preview_settings;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        preview_settings = preview_settings_;
    }

    if (preview_settings.mode != PreviewMode::None) {
        SDWrapper::set_preview_callback(
            [this](int step, int frame_count, const std::vector<uint8_t>& jpeg_data,
                   int width, int height, bool is_noisy) {
                update_preview(step, frame_count, jpeg_data, width, height, is_noisy);
            },
            preview_settings.mode,
            preview_settings.interval,
            preview_settings.max_size,
            preview_settings.quality
        );
    }

    std::vector<std::string> outputs;
    
    try {
        switch (type) {
            case GenerationType::Text2Image:
                outputs = process_txt2img_unlocked(params, job_id);
                break;
            case GenerationType::Image2Image:
                outputs = process_img2img_unlocked(params, job_id);
                break;
            case GenerationType::Text2Video:
                outputs = process_txt2vid_unlocked(params, job_id);
                break;
            case GenerationType::Upscale:
                outputs = process_upscale_unlocked(params, job_id);
                break;
            case GenerationType::Convert:
                outputs = process_convert_unlocked(params, job_id);
                break;
            case GenerationType::ModelDownload:
                outputs = process_model_download_unlocked(params, job_id);
                break;
            case GenerationType::ModelHash:
                outputs = process_model_hash_unlocked(params, job_id);
                break;
        }
    } catch (...) {
        SDWrapper::clear_progress_callback();
        SDWrapper::clear_preview_callback();
        throw;
    }

    SDWrapper::clear_progress_callback();
    SDWrapper::clear_preview_callback();
    return outputs;
}

void QueueManager::update_progress(int step, int total_steps) {
    std::string job_id;
    bool should_broadcast = false;

    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        // Always update internal state (for polling queries)
        current_progress_.step = step;
        current_progress_.total_steps = total_steps;

        // Throttle broadcasts to reduce callback overhead
        auto now = std::chrono::steady_clock::now();
        if (now - last_progress_broadcast_ >= PROGRESS_THROTTLE_MS) {
            last_progress_broadcast_ = now;
            should_broadcast = true;
            job_id = current_job_id_;
        }
    }

    // Broadcast progress via WebSocket only if not throttled
    if (should_broadcast && !job_id.empty()) {
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::JobProgress, {
                {"job_id", job_id},
                {"step", step},
                {"total_steps", total_steps}
            });
        }
    }
}

void QueueManager::set_batch_info(int /*total_images*/) {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    // Reset progress for new job
    current_progress_.step = 0;
    current_progress_.total_steps = 0;
}

void QueueManager::update_preview(int step, int frame_count, const std::vector<uint8_t>& jpeg_data,
                                   int width, int height, bool is_noisy) {
    std::string job_id;
    bool should_broadcast = false;

    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        job_id = current_job_id_;
    }

    if (job_id.empty() || jpeg_data.empty()) {
        return;
    }

    // Always store preview in buffer (for HTTP endpoint access)
    {
        std::lock_guard<std::mutex> lock(preview_buffer_mutex_);
        auto& buffer = preview_buffers_[job_id];
        buffer.jpeg_data = jpeg_data;
        buffer.width = width;
        buffer.height = height;
        buffer.step = step;
        buffer.frame_count = frame_count;
        buffer.is_noisy = is_noisy;
    }

    // Rate limit WebSocket notifications
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        auto now = std::chrono::steady_clock::now();
        if (now - last_preview_broadcast_ >= PREVIEW_THROTTLE_MS) {
            last_preview_broadcast_ = now;
            should_broadcast = true;
        }
    }

    // Broadcast URL notification only (client fetches via HTTP)
    if (should_broadcast) {
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::JobPreview, {
                {"job_id", job_id},
                {"step", step},
                {"frame_count", frame_count},
                {"width", width},
                {"height", height},
                {"is_noisy", is_noisy},
                {"preview_url", "/jobs/" + job_id + "/preview"}
            });
        }
    }
}

std::optional<QueueManager::PreviewBuffer> QueueManager::get_preview(const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(preview_buffer_mutex_);
    auto it = preview_buffers_.find(job_id);
    if (it == preview_buffers_.end() || it->second.jpeg_data.empty()) {
        return std::nullopt;
    }
    return it->second;
}

void QueueManager::clear_preview_buffer(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(preview_buffer_mutex_);
    preview_buffers_.erase(job_id);
}

void QueueManager::set_preview_settings(PreviewMode mode, int interval, int max_size, int quality) {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    preview_settings_.mode = mode;
    preview_settings_.interval = interval;
    preview_settings_.max_size = max_size;
    preview_settings_.quality = quality;
}

QueueManager::PreviewSettings QueueManager::get_preview_settings() const {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    return preview_settings_;
}

void QueueManager::update_job_params(const std::string& job_id, const nlohmann::json& params) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto it = jobs_.find(job_id);
    if (it != jobs_.end()) {
        it->second.params = params;
    }
}

std::vector<std::string> QueueManager::process_txt2img_unlocked(
    const nlohmann::json& job_params,
    const std::string& job_id
) {
    auto params = Txt2ImgParams::from_json(job_params);

    // Store properly typed params back to the job
    nlohmann::json full_params = params.to_json();
    update_job_params(job_id, full_params);

    // Set batch info for progress tracking
    set_batch_info(params.batch_count);

    std::lock_guard<std::mutex> ctx_lock(model_manager_.get_context_mutex());
    auto* ctx = model_manager_.get_context();

    if (!ctx) {
        throw std::runtime_error("No model loaded");
    }

    auto outputs = SDWrapper::generate_txt2img(
        ctx, params,
        model_manager_.get_lora_dir(),
        output_dir_,
        job_id
    );

    // Save config.json with all parameters (including defaults)
    save_job_config(job_id, GenerationType::Text2Image, full_params);

    return outputs;
}

std::vector<std::string> QueueManager::process_img2img_unlocked(
    const nlohmann::json& job_params,
    const std::string& job_id
) {
    auto params = Img2ImgParams::from_json(job_params);

    // Store properly typed params back to the job
    nlohmann::json full_params = params.to_json();
    update_job_params(job_id, full_params);

    // Set batch info for progress tracking
    set_batch_info(params.batch_count);

    std::lock_guard<std::mutex> ctx_lock(model_manager_.get_context_mutex());
    auto* ctx = model_manager_.get_context();

    if (!ctx) {
        throw std::runtime_error("No model loaded");
    }

    auto outputs = SDWrapper::generate_img2img(
        ctx, params,
        model_manager_.get_lora_dir(),
        output_dir_,
        job_id
    );

    // Save config.json with all parameters (including defaults)
    save_job_config(job_id, GenerationType::Image2Image, full_params);

    return outputs;
}

std::vector<std::string> QueueManager::process_txt2vid_unlocked(
    const nlohmann::json& job_params,
    const std::string& job_id
) {
    auto params = Txt2VidParams::from_json(job_params);

    // Store properly typed params back to the job
    nlohmann::json full_params = params.to_json();
    update_job_params(job_id, full_params);

    // Set batch info for progress tracking (video is always 1 output)
    set_batch_info(1);

    std::lock_guard<std::mutex> ctx_lock(model_manager_.get_context_mutex());
    auto* ctx = model_manager_.get_context();

    if (!ctx) {
        throw std::runtime_error("No model loaded");
    }

    auto outputs = SDWrapper::generate_txt2vid(
        ctx, params,
        model_manager_.get_lora_dir(),
        output_dir_,
        job_id
    );

    // Save config.json with all parameters (including defaults)
    save_job_config(job_id, GenerationType::Text2Video, full_params);

    return outputs;
}

std::vector<std::string> QueueManager::process_upscale_unlocked(
    const nlohmann::json& job_params,
    const std::string& job_id
) {
    auto params = UpscaleParams::from_json(job_params);

    // Store properly typed params back to the job
    nlohmann::json full_params = params.to_json();
    update_job_params(job_id, full_params);

    // Set batch info for progress tracking (upscale is always 1 image)
    set_batch_info(1);

    // Check if auto_unload is requested
    bool auto_unload = true;
    if (job_params.contains("auto_unload")) {
        auto_unload = job_params["auto_unload"].get<bool>();
    }

    // Get upscaler context with brief lock - don't hold mutex during the actual upscale
    // operation as it can take minutes and would block health endpoint checks.
    // This is safe because the queue worker is single-threaded, so no concurrent
    // load/unload can happen while a job is processing.
    upscaler_ctx_t* upscaler_ctx = nullptr;
    {
        std::lock_guard<std::mutex> ctx_lock(model_manager_.get_upscaler_mutex());
        upscaler_ctx = model_manager_.get_upscaler_context();
    }

    if (!upscaler_ctx) {
        throw std::runtime_error("No upscaler loaded. Load an ESRGAN model first using /upscaler/load");
    }

    auto outputs = SDWrapper::upscale_image(
        upscaler_ctx, params,
        output_dir_,
        job_id
    );

    // Save config.json with all parameters (including defaults)
    save_job_config(job_id, GenerationType::Upscale, full_params);

    // Auto unload if requested (must release lock first, then re-acquire through unload_upscaler)
    // Note: This is a bit awkward but necessary for proper lock management
    if (auto_unload) {
        // We can't unload while holding the lock, so we'll do it after returning
        // The caller can check auto_unload and unload separately
        // Actually, let's handle this differently - we'll mark it and let the worker handle it
    }

    return outputs;
}

std::vector<std::string> QueueManager::process_convert_unlocked(
    const nlohmann::json& job_params,
    const std::string& /*job_id*/
) {
    // Get input path (required)
    if (!job_params.contains("input_path") || job_params["input_path"].get<std::string>().empty()) {
        throw std::runtime_error("input_path is required");
    }
    std::string input_path = job_params["input_path"].get<std::string>();

    // Get output path (required)
    if (!job_params.contains("output_path") || job_params["output_path"].get<std::string>().empty()) {
        throw std::runtime_error("output_path is required");
    }
    std::string output_path = job_params["output_path"].get<std::string>();

    // Get quantization type (required)
    if (!job_params.contains("output_type") || job_params["output_type"].get<std::string>().empty()) {
        throw std::runtime_error("output_type is required");
    }
    std::string output_type_str = job_params["output_type"].get<std::string>();

    // Optional VAE path
    std::string vae_path = "";
    if (job_params.contains("vae_path") && !job_params["vae_path"].is_null()) {
        vae_path = job_params["vae_path"].get<std::string>();
    }

    // Optional tensor type rules
    std::string tensor_type_rules = "";
    if (job_params.contains("tensor_type_rules") && !job_params["tensor_type_rules"].is_null()) {
        tensor_type_rules = job_params["tensor_type_rules"].get<std::string>();
    }

    // Verify input file exists
    if (!std::filesystem::exists(input_path)) {
        throw std::runtime_error("Input file does not exist: " + input_path);
    }

    std::cout << "[QueueManager] Converting model: " << input_path << " -> " << output_path
              << " (type: " << output_type_str << ")" << std::endl;

    // Convert the model using SDWrapper
    bool success = SDWrapper::convert_model(
        input_path,
        vae_path,
        output_path,
        output_type_str,
        tensor_type_rules
    );

    if (!success) {
        throw std::runtime_error("Model conversion failed");
    }

    std::cout << "[QueueManager] Conversion complete: " << output_path << std::endl;

    // Rescan models to pick up the new converted file
    model_manager_.scan_models();

    // Return the output path as the result
    return { output_path };
}

void QueueManager::save_job_config(const std::string& job_id, GenerationType type, const nlohmann::json& params) {
    namespace fs = std::filesystem;

    // Build the job output directory path
    fs::path job_dir = fs::path(output_dir_) / job_id;

    // Only save if the directory exists (job produced output)
    if (!fs::exists(job_dir)) {
        return;
    }

    // Get the job's model settings
    nlohmann::json model_settings;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        auto it = jobs_.find(job_id);
        if (it != jobs_.end()) {
            model_settings = it->second.model_settings;
        }
    }

    // Build the config JSON with metadata
    nlohmann::json config;
    config["job_id"] = job_id;
    config["type"] = generation_type_to_string(type);
    config["created_at"] = utils::time_to_string(utils::get_time_now());
    config["params"] = params;
    if (!model_settings.empty()) {
        config["model_settings"] = model_settings;
    }

    // Write to config.json in the job folder
    fs::path config_path = job_dir / "config.json";
    std::ofstream file(config_path);
    if (file.is_open()) {
        file << config.dump(2);
        std::cout << "[QueueManager] Saved job config to: " << config_path.string() << std::endl;
    }
}

void QueueManager::save_state() {
    nlohmann::json state;
    nlohmann::json items_array = nlohmann::json::array();
    
    for (const auto& [id, item] : jobs_) {
        items_array.push_back(item.to_json());
    }
    
    state["items"] = items_array;
    
    std::ofstream file(state_file_);
    if (file.is_open()) {
        file << state.dump(2);
    }
}

void QueueManager::load_state() {
    if (!utils::file_exists(state_file_)) {
        return;
    }
    
    try {
        std::ifstream file(state_file_);
        nlohmann::json state;
        file >> state;
        
        if (state.contains("items")) {
            for (const auto& j : state["items"]) {
                QueueItem item = QueueItem::from_json(j);
                
                // Reset processing jobs to pending
                if (item.status == QueueStatus::Processing) {
                    item.status = QueueStatus::Pending;
                    pending_queue_.push(item.job_id);
                } else if (item.status == QueueStatus::Pending) {
                    pending_queue_.push(item.job_id);
                }
                
                jobs_[item.job_id] = item;
            }
        }
        
        std::cout << "[QueueManager] Loaded " << jobs_.size() << " jobs from state file" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[QueueManager] Failed to load state: " << e.what() << std::endl;
    }
}

std::pair<std::string, std::string> QueueManager::add_download_job(const nlohmann::json& params) {
    // Create download job
    std::string download_job_id = add_job(GenerationType::ModelDownload, params);

    // Create hash job linked to download job
    nlohmann::json hash_params = {
        {"file_path", ""},  // Will be filled by download job
        {"model_type", params.value("model_type", "")},
        {"download_job_id", download_job_id}
    };

    QueueItem hash_item;
    hash_item.job_id = utils::generate_uuid();
    hash_item.type = GenerationType::ModelHash;
    hash_item.status = QueueStatus::Pending;
    hash_item.params = hash_params;
    hash_item.linked_job_id = download_job_id;
    hash_item.created_at = std::chrono::system_clock::now();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        jobs_[hash_item.job_id] = hash_item;
        // Don't add to pending queue yet - it will be added after download completes
    }

    // Update download job with linked hash job ID
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (jobs_.count(download_job_id)) {
            jobs_[download_job_id].linked_job_id = hash_item.job_id;
        }
    }

    save_state();

    // Broadcast job added events via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::JobAdded, {
            {"job_id", download_job_id},
            {"type", "model_download"},
            {"queue_position", pending_queue_.size()}
        });
        ws->broadcast(WSEventType::JobAdded, {
            {"job_id", hash_item.job_id},
            {"type", "model_hash"},
            {"queue_position", pending_queue_.size() + 1}  // Hash job waits for download
        });
    }

    return {download_job_id, hash_item.job_id};
}

void QueueManager::fail_linked_job(const std::string& job_id, const std::string& error_message) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = jobs_.find(job_id);
    if (it != jobs_.end() && it->second.status == QueueStatus::Pending) {
        it->second.status = QueueStatus::Failed;
        it->second.error_message = error_message;
        it->second.completed_at = std::chrono::system_clock::now();

        // Broadcast job status change via WebSocket
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::JobStatusChanged, {
                {"job_id", job_id},
                {"status", "failed"},
                {"error_message", error_message}
            });
        }
    }
}

std::vector<std::string> QueueManager::process_model_download_unlocked(
    const nlohmann::json& params,
    const std::string& job_id
) {
    std::vector<std::string> outputs;

    // Get download parameters
    std::string source = params.value("source", "url");
    std::string model_type = params.value("model_type", "checkpoint");
    std::string subfolder = params.value("subfolder", "");

    // Get linked hash job ID
    std::string hash_job_id;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (jobs_.count(job_id)) {
            hash_job_id = jobs_[job_id].linked_job_id;
        }
    }

    try {
        // Get paths config from model manager
        nlohmann::json paths_config = model_manager_.get_paths_config();
        DownloadManager download_manager(paths_config);

        DownloadResult result;

        // Update progress callback for download
        auto progress_callback = [this](size_t downloaded, size_t total, size_t /*speed*/) {
            int progress = 0;
            if (total > 0) {
                progress = static_cast<int>((downloaded * 100) / total);
            }
            update_progress(progress, 100);
        };

        if (source == "civitai") {
            std::string model_id = params.value("model_id", "");
            if (model_id.empty()) {
                throw std::runtime_error("CivitAI model_id is required");
            }
            result = download_manager.download_from_civitai(model_id, model_type, subfolder, progress_callback);

        } else if (source == "huggingface" || source == "hf") {
            std::string repo_id = params.value("repo_id", "");
            std::string filename = params.value("filename", "");
            std::string revision = params.value("revision", "main");

            if (repo_id.empty() || filename.empty()) {
                throw std::runtime_error("HuggingFace repo_id and filename are required");
            }
            result = download_manager.download_from_huggingface(
                repo_id, filename, model_type, subfolder, revision, progress_callback);

        } else {
            // Direct URL download
            std::string url = params.value("url", "");
            std::string filename = params.value("filename", "");

            if (url.empty()) {
                throw std::runtime_error("Download URL is required");
            }

            // Validate URL
            if (!download_manager.validate_model_url(url) && filename.empty()) {
                throw std::runtime_error("URL does not appear to point to a supported model file. "
                    "Supported extensions: .safetensors, .ckpt, .pt, .pth, .bin, .gguf");
            }

            result = download_manager.download_from_url(url, model_type, subfolder, filename, progress_callback);
        }

        if (!result.success) {
            // Fail the linked hash job
            if (!hash_job_id.empty()) {
                fail_linked_job(hash_job_id, "Download failed: " + result.error_message);
            }
            throw std::runtime_error(result.error_message);
        }

        outputs.push_back(result.file_path);

        // Update hash job with file path and add to pending queue
        if (!hash_job_id.empty()) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (jobs_.count(hash_job_id)) {
                jobs_[hash_job_id].params["file_path"] = result.file_path;
                jobs_[hash_job_id].params["file_name"] = result.file_name;
                jobs_[hash_job_id].params["metadata"] = result.metadata;
                pending_queue_.push(hash_job_id);
            }
            queue_cv_.notify_one();
        }

        // Rescan models to pick up the new file
        model_manager_.scan_models();

    } catch (const std::exception& e) {
        // Fail the linked hash job
        if (!hash_job_id.empty()) {
            fail_linked_job(hash_job_id, "Download failed: " + std::string(e.what()));
            save_state();
        }
        throw;
    }

    return outputs;
}

std::vector<std::string> QueueManager::process_model_hash_unlocked(
    const nlohmann::json& params,
    const std::string& /*job_id*/
) {
    std::vector<std::string> outputs;

    std::string file_path = params.value("file_path", "");
    if (file_path.empty()) {
        throw std::runtime_error("File path is required for hashing");
    }

    // Check if file exists
    if (!std::filesystem::exists(file_path)) {
        throw std::runtime_error("File not found: " + file_path);
    }

    // Update progress
    update_progress(0, 100);

    // Compute hash
    std::string hash = utils::compute_sha256(file_path);

    update_progress(100, 100);

    // Store the hash in outputs for visibility
    outputs.push_back(hash);

    // Update the model registry with the hash if possible
    std::string model_type = params.value("model_type", "");
    if (!model_type.empty()) {
        // The model manager will pick this up on next scan or we can update it directly
        // For now, we'll just return the hash
    }

    std::cout << "[QueueManager] Computed hash for " << file_path << ": " << hash << std::endl;

    return outputs;
}

} // namespace sdcpp
