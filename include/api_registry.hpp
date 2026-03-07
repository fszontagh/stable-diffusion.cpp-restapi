#pragma once

#include "httplib_compat.h"
#include "api_schema.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace sdcpp {

// Query parameter descriptor for GET endpoints
struct QueryParam {
    std::string name;
    schema::FieldType type;
    std::string description;
    bool required = false;
    nlohmann::json default_value = nullptr;
    std::vector<std::string> enum_values;
};

// Path parameter descriptor
struct PathParam {
    std::string name;
    schema::FieldType type;
    std::string description;
};

// Endpoint metadata stored in the registry
struct EndpointEntry {
    std::string method;          // GET, POST, PUT, DELETE
    std::string path;            // OpenAPI-style path (e.g., /queue/{job_id})
    std::string httplib_pattern; // httplib regex pattern (e.g., R"(/queue/([a-f0-9\-]+))")
    std::string summary;
    std::string description;
    std::string tag;
    int response_code = 200;
    std::string request_schema;  // name in components/schemas (empty = no body)
    std::string response_schema; // name in components/schemas (empty = no structured response)
    std::string response_content_type = "application/json";
    std::vector<QueryParam> query_params;
    std::vector<PathParam> path_params;
    bool deprecated = false;
};

// Fluent builder for endpoint configuration
class EndpointBuilder {
public:
    EndpointBuilder(EndpointEntry& entry) : entry_(entry) {}

    EndpointBuilder& description(const std::string& desc) {
        entry_.description = desc;
        return *this;
    }

    EndpointBuilder& query(const std::string& name, schema::FieldType type,
                           const std::string& desc, bool required = false,
                           const nlohmann::json& default_value = nullptr) {
        entry_.query_params.push_back({name, type, desc, required, default_value, {}});
        return *this;
    }

    EndpointBuilder& query_enum(const std::string& name, const std::string& desc,
                                const std::vector<std::string>& values, bool required = false) {
        entry_.query_params.push_back({name, schema::FieldType::Enum, desc, required, nullptr, values});
        return *this;
    }

    EndpointBuilder& path_param(const std::string& name, schema::FieldType type,
                                const std::string& desc) {
        entry_.path_params.push_back({name, type, desc});
        return *this;
    }

    EndpointBuilder& response_type(const std::string& content_type) {
        entry_.response_content_type = content_type;
        return *this;
    }

    EndpointBuilder& deprecated(bool val = true) {
        entry_.deprecated = val;
        return *this;
    }

private:
    EndpointEntry& entry_;
};

/**
 * API Registry - registers routes and generates OpenAPI 3.1 schema
 */
class ApiRegistry {
public:
    ApiRegistry(const std::string& title, const std::string& version,
                const std::string& description = "");

    // Register an endpoint with request and response schema types
    template<typename ReqSchema = void, typename ResSchema = void>
    EndpointBuilder addEndpoint(
        httplib::Server& server,
        const std::string& method,
        const std::string& path,
        const std::string& summary,
        const std::string& tag,
        int response_code,
        std::function<void(const httplib::Request&, httplib::Response&)> handler);

    // Register an endpoint without schema types (for binary responses, etc.)
    EndpointBuilder addEndpointRaw(
        httplib::Server& server,
        const std::string& method,
        const std::string& path,
        const std::string& httplib_pattern,
        const std::string& summary,
        const std::string& tag,
        int response_code,
        std::function<void(const httplib::Request&, httplib::Response&)> handler);

    // Register a schema without an endpoint (for shared components)
    void registerSchema(const std::string& name, const schema::SchemaDescriptor& desc);

    // Generate the complete OpenAPI 3.1 specification
    nlohmann::json generateOpenApiSpec() const;

    // Register the /openapi.json endpoint on the server
    void serveOpenApiSpec(httplib::Server& server, const std::string& path = "/openapi.json");

private:
    // Convert OpenAPI-style path to httplib regex pattern
    static std::string path_to_httplib_pattern(const std::string& path);

    // Extract path parameter names from OpenAPI-style path
    static std::vector<std::string> extract_path_params(const std::string& path);

    // Register route with httplib server
    void register_route(httplib::Server& server, const std::string& method,
                        const std::string& pattern,
                        std::function<void(const httplib::Request&, httplib::Response&)> handler);

    // Collect schema from a type if it has static schema()
    template<typename T>
    void collect_schema_if_available();

    std::string title_;
    std::string version_;
    std::string description_;
    std::vector<EndpointEntry> endpoints_;
    std::map<std::string, schema::SchemaDescriptor> schemas_;
};

// Template implementation

template<typename ReqSchema, typename ResSchema>
EndpointBuilder ApiRegistry::addEndpoint(
    httplib::Server& server,
    const std::string& method,
    const std::string& path,
    const std::string& summary,
    const std::string& tag,
    int response_code,
    std::function<void(const httplib::Request&, httplib::Response&)> handler)
{
    EndpointEntry entry;
    entry.method = method;
    entry.path = path;
    entry.httplib_pattern = path_to_httplib_pattern(path);
    entry.summary = summary;
    entry.tag = tag;
    entry.response_code = response_code;

    // Collect request schema
    if constexpr (!std::is_void_v<ReqSchema>) {
        if constexpr (schema::has_schema_v<ReqSchema>) {
            auto desc = ReqSchema::schema();
            entry.request_schema = desc.name;
            if (schemas_.find(desc.name) == schemas_.end()) {
                schemas_[desc.name] = desc;
            }
            // Also collect base schema if inherited
            collect_schema_if_available<ReqSchema>();
        }
    }

    // Collect response schema
    if constexpr (!std::is_void_v<ResSchema>) {
        if constexpr (schema::has_schema_v<ResSchema>) {
            auto desc = ResSchema::schema();
            entry.response_schema = desc.name;
            if (schemas_.find(desc.name) == schemas_.end()) {
                schemas_[desc.name] = desc;
            }
            collect_schema_if_available<ResSchema>();
        }
    }

    // Register route with httplib
    register_route(server, method, entry.httplib_pattern, handler);

    endpoints_.push_back(entry);
    return EndpointBuilder(endpoints_.back());
}

// Trait to check if a type has a base_schema_type typedef
template<typename T, typename = void>
struct has_base_schema : std::false_type {};

template<typename T>
struct has_base_schema<T, std::void_t<typename T::base_schema_type>> : std::true_type {};

template<typename T>
void ApiRegistry::collect_schema_if_available() {
    if constexpr (schema::has_schema_v<T>) {
        auto desc = T::schema();
        if (schemas_.find(desc.name) == schemas_.end()) {
            schemas_[desc.name] = desc;
        }
        // Recursively collect base schemas
        if constexpr (has_base_schema<T>::value) {
            collect_schema_if_available<typename T::base_schema_type>();
        }
    }
}

} // namespace sdcpp
