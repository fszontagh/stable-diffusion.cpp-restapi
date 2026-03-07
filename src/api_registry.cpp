#include "api_registry.hpp"
#include <regex>
#include <iostream>

namespace sdcpp {

ApiRegistry::ApiRegistry(const std::string& title, const std::string& version,
                         const std::string& description)
    : title_(title), version_(version), description_(description) {}

std::string ApiRegistry::path_to_httplib_pattern(const std::string& path) {
    // Convert OpenAPI path params like /queue/{job_id} to httplib regex /queue/([a-f0-9\-]+)
    // For paths without params, return as-is
    std::string result = path;

    // Replace {job_id} style params with appropriate regex
    static const std::vector<std::pair<std::string, std::string>> param_patterns = {
        {"{job_id}", "([a-f0-9\\-]+)"},
        {"{model_type}", "([^/]+)"},
        {"{model_name}", "(.+)"},
        {"{path}", "(.*)"},
        {"{id}", "(\\d+(?:(?::|%3A)\\d+)?)"},
        {"{mode}", "([^/]+)"},
    };

    for (const auto& [param, regex] : param_patterns) {
        size_t pos = result.find(param);
        if (pos != std::string::npos) {
            result.replace(pos, param.length(), regex);
        }
    }

    return result;
}

std::vector<std::string> ApiRegistry::extract_path_params(const std::string& path) {
    std::vector<std::string> params;
    std::regex param_regex(R"(\{(\w+)\})");
    auto begin = std::sregex_iterator(path.begin(), path.end(), param_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        params.push_back((*it)[1].str());
    }
    return params;
}

void ApiRegistry::register_route(httplib::Server& server, const std::string& method,
                                  const std::string& pattern,
                                  std::function<void(const httplib::Request&, httplib::Response&)> handler) {
    if (method == "GET") {
        server.Get(pattern, handler);
    } else if (method == "POST") {
        server.Post(pattern, handler);
    } else if (method == "PUT") {
        server.Put(pattern, handler);
    } else if (method == "DELETE") {
        server.Delete(pattern, handler);
    } else if (method == "PATCH") {
        server.Patch(pattern, handler);
    } else {
        std::cerr << "[ApiRegistry] Unknown HTTP method: " << method << " for " << pattern << std::endl;
    }
}

EndpointBuilder ApiRegistry::addEndpointRaw(
    httplib::Server& server,
    const std::string& method,
    const std::string& path,
    const std::string& httplib_pattern,
    const std::string& summary,
    const std::string& tag,
    int response_code,
    std::function<void(const httplib::Request&, httplib::Response&)> handler)
{
    EndpointEntry entry;
    entry.method = method;
    entry.path = path;
    entry.httplib_pattern = httplib_pattern;
    entry.summary = summary;
    entry.tag = tag;
    entry.response_code = response_code;

    register_route(server, method, httplib_pattern, handler);

    endpoints_.push_back(entry);
    return EndpointBuilder(endpoints_.back());
}

void ApiRegistry::registerSchema(const std::string& name, const schema::SchemaDescriptor& desc) {
    schemas_[name] = desc;
}

nlohmann::json ApiRegistry::generateOpenApiSpec() const {
    nlohmann::json spec;

    // OpenAPI metadata
    spec["openapi"] = "3.1.0";
    spec["info"] = {
        {"title", title_},
        {"version", version_}
    };
    if (!description_.empty()) {
        spec["info"]["description"] = description_;
    }

    // Paths
    nlohmann::json paths = nlohmann::json::object();

    for (const auto& ep : endpoints_) {
        nlohmann::json operation;
        operation["summary"] = ep.summary;
        operation["tags"] = nlohmann::json::array({ep.tag});

        if (!ep.description.empty()) {
            operation["description"] = ep.description;
        }

        if (ep.deprecated) {
            operation["deprecated"] = true;
        }

        // Path parameters
        nlohmann::json parameters = nlohmann::json::array();

        for (const auto& pp : ep.path_params) {
            nlohmann::json param;
            param["name"] = pp.name;
            param["in"] = "path";
            param["required"] = true;
            param["description"] = pp.description;
            param["schema"] = {{"type", schema::field_type_to_string(pp.type)}};
            parameters.push_back(param);
        }

        // Auto-detect path params from path pattern if not explicitly set
        if (ep.path_params.empty()) {
            auto detected = extract_path_params(ep.path);
            for (const auto& name : detected) {
                nlohmann::json param;
                param["name"] = name;
                param["in"] = "path";
                param["required"] = true;
                param["schema"] = {{"type", "string"}};
                parameters.push_back(param);
            }
        }

        // Query parameters
        for (const auto& qp : ep.query_params) {
            nlohmann::json param;
            param["name"] = qp.name;
            param["in"] = "query";
            param["required"] = qp.required;
            param["description"] = qp.description;

            nlohmann::json param_schema;
            if (qp.type == schema::FieldType::Enum) {
                param_schema["type"] = "string";
                if (!qp.enum_values.empty()) {
                    param_schema["enum"] = qp.enum_values;
                }
            } else {
                param_schema["type"] = schema::field_type_to_string(qp.type);
            }
            if (!qp.default_value.is_null()) {
                param_schema["default"] = qp.default_value;
            }
            param["schema"] = param_schema;
            parameters.push_back(param);
        }

        if (!parameters.empty()) {
            operation["parameters"] = parameters;
        }

        // Request body
        if (!ep.request_schema.empty()) {
            operation["requestBody"] = {
                {"required", true},
                {"content", {
                    {"application/json", {
                        {"schema", {{"$ref", "#/components/schemas/" + ep.request_schema}}}
                    }}
                }}
            };
        }

        // Response
        nlohmann::json response;
        if (!ep.response_schema.empty()) {
            if (ep.response_content_type == "application/json") {
                response["content"] = {
                    {"application/json", {
                        {"schema", {{"$ref", "#/components/schemas/" + ep.response_schema}}}
                    }}
                };
            } else {
                response["content"] = {
                    {ep.response_content_type, {
                        {"schema", {{"type", "string"}, {"format", "binary"}}}
                    }}
                };
            }
        }

        // Add standard status description
        std::string status_desc;
        switch (ep.response_code) {
            case 200: status_desc = "Successful operation"; break;
            case 201: status_desc = "Created"; break;
            case 202: status_desc = "Accepted (job queued)"; break;
            case 204: status_desc = "No content"; break;
            default:  status_desc = "Response"; break;
        }
        response["description"] = status_desc;

        operation["responses"] = {
            {std::to_string(ep.response_code), response},
            {"400", {{"description", "Bad request"}, {"content", {{"application/json", {{"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}}}}}}},
            {"500", {{"description", "Internal server error"}, {"content", {{"application/json", {{"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}}}}}}}
        };

        // Convert method to lowercase for OpenAPI
        std::string method_lower = ep.method;
        std::transform(method_lower.begin(), method_lower.end(), method_lower.begin(), ::tolower);

        paths[ep.path][method_lower] = operation;
    }

    spec["paths"] = paths;

    // Components/schemas
    nlohmann::json components_schemas = nlohmann::json::object();

    // Always include ErrorResponse
    components_schemas["ErrorResponse"] = {
        {"type", "object"},
        {"properties", {
            {"error", {{"type", "string"}, {"description", "Error message"}}}
        }},
        {"required", nlohmann::json::array({"error"})}
    };

    for (const auto& [name, desc] : schemas_) {
        components_schemas[name] = desc.to_json_schema();
    }

    spec["components"] = {{"schemas", components_schemas}};

    return spec;
}

void ApiRegistry::serveOpenApiSpec(httplib::Server& server, const std::string& path) {
    server.Get(path, [this](const httplib::Request& /*req*/, httplib::Response& res) {
        auto spec = generateOpenApiSpec();
        res.set_content(spec.dump(2), "application/json");
    });
}

} // namespace sdcpp
