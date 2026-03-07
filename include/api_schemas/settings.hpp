#pragma once

#include "api_schemas/common.hpp"

namespace sdcpp {
namespace api {

struct PreviewSettingsResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("PreviewSettingsResponse", "Preview generation settings")
            .required_field("enabled", schema::FieldType::Boolean, "Whether live preview is enabled")
            .required_enum("mode", "Preview decode mode", PREVIEW_MODE_VALUES)
            .required_field("interval", schema::FieldType::Integer, "Preview interval (every N steps)")
            .optional_field("max_size", schema::FieldType::Integer, "Maximum preview dimension")
            .optional_field("quality", schema::FieldType::Integer, "JPEG quality (1-100)")
            .build();
    }
};

struct UpdatePreviewSettingsRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("UpdatePreviewSettingsRequest", "Update preview settings")
            .optional_field("enabled", schema::FieldType::Boolean, "Enable/disable live preview")
            .enum_field("mode", "Preview decode mode", PREVIEW_MODE_VALUES)
            .optional_field("interval", schema::FieldType::Integer, "Preview interval (every N steps)")
            .optional_field("max_size", schema::FieldType::Integer, "Maximum preview dimension")
            .optional_field("quality", schema::FieldType::Integer, "JPEG quality (1-100)")
            .build();
    }
};

struct ArchitecturesResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("ArchitecturesResponse", "Available model architecture presets")
            .object_field("architectures", "Architecture presets keyed by ID", true)
            .build();
    }
};

struct DetectArchitectureResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("DetectArchitectureResponse", "Architecture detection result")
            .required_field("detected", schema::FieldType::Boolean, "Whether architecture was detected")
            .object_field("architecture", "Detected architecture details (null if not detected)")
            .build();
    }
};

struct ModelRefreshResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("ModelRefreshResponse", "Model list refresh result")
            .required_field("success", schema::FieldType::Boolean, "Whether refresh succeeded")
            .required_field("message", schema::FieldType::String, "Status message")
            .object_field("models", "Updated model list")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
