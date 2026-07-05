#pragma once

#include "api_schema.hpp"

namespace sdcpp {
namespace api {

// Common enum values used across multiple schemas
inline const std::vector<std::string> SAMPLER_VALUES = {
    "euler", "euler_a", "heun", "dpm2", "dpm++2s_a", "dpm++2m", "dpm++2mv2",
    "ipndm", "ipndm_v", "lcm", "ddim_trailing", "tcd", "res_multistep", "res_2s",
    "er_sde", "euler_cfg_pp", "euler_a_cfg_pp", "euler_ge",
    // leejet PR #1742 (DPM++ 2M SDE) and #1743 (Brownian-tree variant).
    "dpm++2m_sde", "dpm++2m_sde_bt"
};

inline const std::vector<std::string> SCHEDULER_VALUES = {
    "discrete", "karras", "exponential", "ays", "gits", "sgm_uniform",
    "simple", "smoothstep", "kl_optimal", "lcm", "bong_tangent", "ltx2",
    "logit_normal",
    // Added in leejet master post-c179075:
    "flux",         // PR #1723
    "flux2",        // PR #1722
    "beta",         // PR #811
    "normal"        // PR #1724 — alias for "discrete"
};

inline const std::vector<std::string> MODEL_TYPE_VALUES = {
    "checkpoint", "diffusion", "vae", "lora", "clip", "t5",
    "embedding", "controlnet", "llm", "esrgan", "taesd"
};

inline const std::vector<std::string> WEIGHT_TYPE_VALUES = {
    "f32", "f16", "bf16", "q8_0", "q5_0", "q5_1", "q4_0", "q4_1",
    "q4_k", "q5_k", "q6_k", "q8_k", "q3_k", "q2_k", "mxfp4", "nvfp4", "q1_0"
};

inline const std::vector<std::string> RNG_TYPE_VALUES = {
    "cuda", "std_default", "cpu"
};

inline const std::vector<std::string> JOB_STATUS_VALUES = {
    "pending", "processing", "completed", "failed", "cancelled", "deleted"
};

inline const std::vector<std::string> JOB_TYPE_VALUES = {
    "txt2img", "img2img", "txt2vid", "upscale", "convert", "model_download", "model_hash"
};

inline const std::vector<std::string> PREVIEW_MODE_VALUES = {
    "none", "proj", "tae", "vae"
};

#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
inline const std::vector<std::string> OFFLOAD_MODE_VALUES = {
    "none", "cond_only", "cond_diffusion", "aggressive", "layer_streaming"
};

inline const std::vector<std::string> VRAM_ESTIMATION_VALUES = {
    "dryrun", "formula"
};
#endif

// Shared response schemas

struct SuccessResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("SuccessResponse", "Standard success response")
            .required_field("success", schema::FieldType::Boolean, "Whether the operation succeeded")
            .required_field("message", schema::FieldType::String, "Human-readable status message")
            .build();
    }
};

struct JobCreatedResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("JobCreatedResponse", "Response when a job is queued")
            .required_field("job_id", schema::FieldType::String, "UUID of the created job")
            .required_field("status", schema::FieldType::String, "Job status (pending)")
            .required_field("position", schema::FieldType::Integer, "Position in queue")
            .build();
    }
};

// ─── Authentication schemas ──────────────────────────────────────────────

struct LoginRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("LoginRequest",
                "Username + password pair for issuing a bearer token")
            .required_field("username", schema::FieldType::String, "Server-configured username")
            .required_field("password", schema::FieldType::String, "Server-configured password")
            .build();
    }
};

struct LoginResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("LoginResponse",
                "Issued bearer token plus its absolute expiration time")
            .required_field("token", schema::FieldType::String,
                "Opaque bearer token (~43 base64url characters)")
            .required_field("expires_at", schema::FieldType::Integer,
                "Token expiration as Unix epoch seconds")
            .required_field("token_type", schema::FieldType::String,
                "Always \"Bearer\" — included to match common OAuth-style clients")
            .build();
    }
};

struct LogoutResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("LogoutResponse", "Response to logout request")
            .required_field("success", schema::FieldType::Boolean, "Always true")
            .required_field("message", schema::FieldType::String, "Confirmation message")
            .build();
    }
};

struct UnauthorizedResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("UnauthorizedResponse",
                "Returned with HTTP 401 when authentication is missing or invalid")
            .required_field("error", schema::FieldType::String, "Short error code")
            .required_field("message", schema::FieldType::String, "Human-readable message")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
