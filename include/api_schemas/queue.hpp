#pragma once

#include "api_schemas/common.hpp"

namespace sdcpp {
namespace api {

struct QueueListResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("QueueListResponse", "Queue listing with pagination and status counts")
            .required_field("pending_count", schema::FieldType::Integer, "Number of pending jobs")
            .required_field("processing_count", schema::FieldType::Integer, "Number of processing jobs")
            .required_field("completed_count", schema::FieldType::Integer, "Number of completed jobs")
            .required_field("failed_count", schema::FieldType::Integer, "Number of failed jobs")
            .required_field("cancelled_count", schema::FieldType::Integer, "Number of cancelled jobs")
            .required_field("total_count", schema::FieldType::Integer, "Total job count")
            .required_field("filtered_count", schema::FieldType::Integer, "Count after filters applied")
            .optional_field("offset", schema::FieldType::Integer, "Pagination offset")
            .optional_field("limit", schema::FieldType::Integer, "Results per page")
            .optional_field("has_more", schema::FieldType::Boolean, "Whether more results exist")
            .optional_field("page", schema::FieldType::Integer, "Current page number")
            .optional_field("total_pages", schema::FieldType::Integer, "Total number of pages")
            .optional_field("has_prev", schema::FieldType::Boolean, "Whether previous page exists")
            .optional_field("newest_timestamp", schema::FieldType::Integer, "Newest job timestamp (unix)")
            .optional_field("oldest_timestamp", schema::FieldType::Integer, "Oldest job timestamp (unix)")
            .object_field("applied_filters", "Active filter parameters")
            .array_field("items", schema::FieldType::Object, "Job entries", true)
            .build();
    }
};

struct JobStatusResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("JobStatusResponse", "Detailed status of a single job")
            .required_field("job_id", schema::FieldType::String, "Job UUID")
            .required_field("type", schema::FieldType::String, "Job type (txt2img, img2img, etc.)")
            .required_field("status", schema::FieldType::String, "Job status")
            .object_field("progress", "Generation progress (step/total_steps)")
            .required_field("created_at", schema::FieldType::String, "Creation timestamp (ISO8601)")
            .optional_field("started_at", schema::FieldType::String, "Start timestamp (ISO8601)")
            .optional_field("completed_at", schema::FieldType::String, "Completion timestamp (ISO8601)")
            .array_field("outputs", schema::FieldType::String, "Output file paths")
            .object_field("params", "Generation parameters used")
            .object_field("model_settings", "Model settings at time of generation")
            .optional_field("error", schema::FieldType::String, "Error message if failed")
            .build();
    }
};

struct DeleteJobsRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("DeleteJobsRequest", "Batch delete jobs request")
            .array_field("job_ids", schema::FieldType::String, "Job UUIDs to delete", true)
            .build();
    }
};

struct DeleteJobsResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("DeleteJobsResponse", "Batch delete result")
            .required_field("success", schema::FieldType::Boolean, "Whether operation completed")
            .required_field("deleted", schema::FieldType::Integer, "Number of jobs deleted")
            .required_field("failed", schema::FieldType::Integer, "Number of deletions that failed")
            .required_field("total", schema::FieldType::Integer, "Total jobs requested")
            .array_field("failed_job_ids", schema::FieldType::String, "IDs of failed deletions")
            .build();
    }
};

struct RecycleBinResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("RecycleBinResponse", "Recycle bin contents")
            .required_field("success", schema::FieldType::Boolean, "Operation success")
            .required_field("enabled", schema::FieldType::Boolean, "Whether recycle bin is enabled")
            .optional_field("retention_minutes", schema::FieldType::Integer, "Auto-purge retention period (minutes)")
            .required_field("count", schema::FieldType::Integer, "Number of items in recycle bin")
            .array_field("items", schema::FieldType::Object, "Deleted job entries")
            .build();
    }
};

struct RecycleBinSettingsResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("RecycleBinSettingsResponse", "Recycle bin configuration")
            .required_field("enabled", schema::FieldType::Boolean, "Whether recycle bin is enabled")
            .required_field("retention_minutes", schema::FieldType::Integer, "Auto-purge retention period")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
