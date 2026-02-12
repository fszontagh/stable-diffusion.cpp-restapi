#pragma once

#include <string>
#include <vector>
#include <sstream>

namespace sdcpp {

/**
 * Queue item field definitions - SINGLE SOURCE OF TRUTH
 * This file defines all field names used in queue items.
 * Edit this file to add/remove fields - changes propagate everywhere.
 */
struct QueueItemFields {
    //==========================================================================
    // FIELD NAME CONSTANTS - Use these throughout the codebase
    //==========================================================================

    // Top-level queue item fields
    static constexpr const char* JOB_ID = "job_id";
    static constexpr const char* TYPE = "type";
    static constexpr const char* STATUS = "status";
    static constexpr const char* PROGRESS = "progress";
    static constexpr const char* CREATED_AT = "created_at";
    static constexpr const char* STARTED_AT = "started_at";
    static constexpr const char* COMPLETED_AT = "completed_at";
    static constexpr const char* ERROR = "error";
    static constexpr const char* OUTPUTS = "outputs";
    static constexpr const char* PARAMS = "params";
    static constexpr const char* MODEL_SETTINGS = "model_settings";
    static constexpr const char* LINKED_JOB_ID = "linked_job_id";

    // Params field names (used inside params object)
    static constexpr const char* PARAM_PROMPT = "prompt";
    static constexpr const char* PARAM_NEGATIVE_PROMPT = "negative_prompt";
    static constexpr const char* PARAM_WIDTH = "width";
    static constexpr const char* PARAM_HEIGHT = "height";
    static constexpr const char* PARAM_STEPS = "steps";
    static constexpr const char* PARAM_CFG_SCALE = "cfg_scale";
    static constexpr const char* PARAM_SEED = "seed";
    static constexpr const char* PARAM_SAMPLER = "sampler";
    static constexpr const char* PARAM_SCHEDULER = "scheduler";
    static constexpr const char* PARAM_BATCH_COUNT = "batch_count";
    static constexpr const char* PARAM_CLIP_SKIP = "clip_skip";
    static constexpr const char* PARAM_STRENGTH = "strength";

    // Model settings field names (used inside model_settings object)
    static constexpr const char* MODEL_NAME = "model_name";
    static constexpr const char* MODEL_ARCHITECTURE = "model_architecture";
    static constexpr const char* CLIP_L_MODEL = "clip_l_model";
    static constexpr const char* CLIP_G_MODEL = "clip_g_model";
    static constexpr const char* T5XXL_MODEL = "t5xxl_model";
    static constexpr const char* VAE_MODEL = "vae_model";
    static constexpr const char* CONTROLNET_MODEL = "controlnet_model";
    static constexpr const char* LORA_MODEL = "lora_model";

    // Type values
    static constexpr const char* TYPE_TXT2IMG = "txt2img";
    static constexpr const char* TYPE_IMG2IMG = "img2img";
    static constexpr const char* TYPE_TXT2VID = "txt2vid";
    static constexpr const char* TYPE_UPSCALE = "upscale";
    static constexpr const char* TYPE_CONVERT = "convert";
    static constexpr const char* TYPE_MODEL_DOWNLOAD = "model_download";
    static constexpr const char* TYPE_MODEL_HASH = "model_hash";

    // Status values
    static constexpr const char* STATUS_PENDING = "pending";
    static constexpr const char* STATUS_PROCESSING = "processing";
    static constexpr const char* STATUS_COMPLETED = "completed";
    static constexpr const char* STATUS_FAILED = "failed";
    static constexpr const char* STATUS_CANCELLED = "cancelled";

    //==========================================================================
    // FIELD DEFINITIONS FOR LLM TOOL DESCRIPTION
    //==========================================================================

    struct Field {
        std::string key;
        std::string values;  // Pipe-separated possible values, empty for free-form
    };

    // Top-level fields for filtering
    static inline const std::vector<Field> FILTER_TOP_LEVEL = {
        {JOB_ID, ""},
        {TYPE, "txt2img|img2img|txt2vid|upscale|convert|model_download|model_hash"},
        {STATUS, "pending|processing|completed|failed|cancelled"},
        {ERROR, ""}
    };

    // Nested params.* fields for filtering
    static inline const std::vector<Field> FILTER_PARAMS = {
        {PARAM_PROMPT, ""},
        {PARAM_NEGATIVE_PROMPT, ""},
        {PARAM_WIDTH, ""},
        {PARAM_HEIGHT, ""},
        {PARAM_STEPS, ""},
        {PARAM_CFG_SCALE, ""},
        {PARAM_SEED, ""},
        {PARAM_SAMPLER, ""},
        {PARAM_SCHEDULER, ""},
        {PARAM_BATCH_COUNT, ""},
        {PARAM_CLIP_SKIP, ""},
        {PARAM_STRENGTH, ""}
    };

    // Nested model_settings.* fields for filtering
    static inline const std::vector<Field> FILTER_MODEL_SETTINGS = {
        {MODEL_NAME, ""},
        {MODEL_ARCHITECTURE, ""},
        {CLIP_L_MODEL, ""},
        {CLIP_G_MODEL, ""},
        {T5XXL_MODEL, ""},
        {VAE_MODEL, ""},
        {CONTROLNET_MODEL, ""},
        {LORA_MODEL, ""}
    };

    /**
     * Generate description string for LLM tool
     */
    static std::string get_filter_keys_description() {
        std::ostringstream desc;
        desc << "Search jobs with dynamic filters. Available filter keys: ";

        // Top-level fields
        desc << "Top-level: ";
        bool first = true;
        for (const auto& f : FILTER_TOP_LEVEL) {
            if (!first) desc << ", ";
            desc << f.key;
            if (!f.values.empty()) {
                desc << " (" << f.values << ")";
            }
            first = false;
        }
        desc << ". ";

        // Params fields
        desc << "Nested params.*: ";
        first = true;
        for (const auto& f : FILTER_PARAMS) {
            if (!first) desc << ", ";
            desc << f.key;
            if (!f.values.empty()) {
                desc << " (" << f.values << ")";
            }
            first = false;
        }
        desc << ". ";

        // Model settings fields
        desc << "Nested model_settings.*: ";
        first = true;
        for (const auto& f : FILTER_MODEL_SETTINGS) {
            if (!first) desc << ", ";
            desc << f.key;
            if (!f.values.empty()) {
                desc << " (" << f.values << ")";
            }
            first = false;
        }
        desc << ". ";

        desc << "Returns minimal metadata + filtered fields. Use get_job for full details.";

        return desc.str();
    }

    /**
     * Get all available field paths (for validation)
     */
    static std::vector<std::string> get_all_field_paths() {
        std::vector<std::string> paths;

        for (const auto& f : FILTER_TOP_LEVEL) {
            paths.push_back(f.key);
        }
        for (const auto& f : FILTER_PARAMS) {
            paths.push_back(std::string(PARAMS) + "." + f.key);
        }
        for (const auto& f : FILTER_MODEL_SETTINGS) {
            paths.push_back(std::string(MODEL_SETTINGS) + "." + f.key);
        }

        return paths;
    }
};

} // namespace sdcpp
