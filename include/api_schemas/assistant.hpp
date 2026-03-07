#pragma once

#include "api_schemas/common.hpp"

namespace sdcpp {
namespace api {

struct AssistantChatRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("AssistantChatRequest", "Chat with assistant LLM")
            .required_field("message", schema::FieldType::String, "User message text")
            .build();
    }
};

struct AssistantStatusResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("AssistantStatusResponse", "Assistant status and model info")
            .required_field("available", schema::FieldType::Boolean, "Whether assistant is available")
            .optional_field("model_loaded", schema::FieldType::Boolean, "Whether LLM is loaded")
            .optional_field("model_name", schema::FieldType::String, "Loaded LLM model name")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
