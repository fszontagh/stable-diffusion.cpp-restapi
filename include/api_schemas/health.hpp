#pragma once

#include "api_schemas/common.hpp"

namespace sdcpp {
namespace api {

struct HealthResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("HealthResponse", "Server health and status information")
            .required_field("status", schema::FieldType::String, "Server status (ok)")
            .required_field("version", schema::FieldType::String, "Server version string")
            .optional_field("git_commit", schema::FieldType::String, "Git commit hash")
            .required_field("model_loaded", schema::FieldType::Boolean, "Whether a model is currently loaded")
            .optional_field("model_loading", schema::FieldType::Boolean, "Whether a model is being loaded")
            .optional_field("loading_model_name", schema::FieldType::String, "Name of model being loaded")
            .optional_field("loading_step", schema::FieldType::Integer, "Current loading step")
            .optional_field("loading_total_steps", schema::FieldType::Integer, "Total loading steps")
            .optional_field("last_error", schema::FieldType::String, "Last error message")
            .optional_field("model_name", schema::FieldType::String, "Name of loaded model")
            .optional_field("model_type", schema::FieldType::String, "Type of loaded model")
            .optional_field("model_architecture", schema::FieldType::String, "Detected architecture")
            .object_field("loaded_components", "Loaded model components (vae, clip_l, etc.)")
            .optional_field("upscaler_loaded", schema::FieldType::Boolean, "Whether an upscaler is loaded")
            .optional_field("upscaler_name", schema::FieldType::String, "Loaded upscaler name")
            .optional_field("ws_enabled", schema::FieldType::Boolean, "Whether WebSocket is enabled")
            .object_field("memory", "System/GPU memory information")
            .object_field("features", "Enabled feature flags")
            .build();
    }
};

struct MemoryResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("MemoryResponse", "Detailed memory usage information")
            .object_field("system", "System RAM usage (total/used/free in bytes and MB)")
            .object_field("process", "Process memory (RSS and virtual)")
            .object_field("gpu", "GPU VRAM usage (if available)")
            .build();
    }
};

struct OptionsResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("OptionsResponse", "Available generation options")
            .array_field("samplers", schema::FieldType::String, "Available sampling algorithms", true)
            .array_field("schedulers", schema::FieldType::String, "Available noise schedulers", true)
            .array_field("quantization_types", schema::FieldType::Object, "Available quantization types", true)
            .build();
    }
};

struct OptionDescriptionsResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("OptionDescriptionsResponse", "Descriptions of all available options")
            .object_field("samplers", "Sampler name to description mapping")
            .object_field("schedulers", "Scheduler name to description mapping")
            .object_field("rng_types", "RNG type name to description mapping")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
