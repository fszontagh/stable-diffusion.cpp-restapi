#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <initializer_list>
#include <cstdint>

namespace sdcpp {
namespace schema {

// JSON Schema type identifiers
enum class FieldType {
    String,
    Integer,
    Number,
    Boolean,
    Array,
    Object,
    Enum
};

inline std::string field_type_to_string(FieldType t) {
    switch (t) {
        case FieldType::String:  return "string";
        case FieldType::Integer: return "integer";
        case FieldType::Number:  return "number";
        case FieldType::Boolean: return "boolean";
        case FieldType::Array:   return "array";
        case FieldType::Object:  return "object";
        case FieldType::Enum:    return "string"; // enums are strings with constraints
    }
    return "string";
}

// Metadata for a single schema field
struct SchemaField {
    std::string name;
    FieldType type = FieldType::String;
    std::string description;
    bool required = false;
    bool arch_default = false; // true = default comes from architecture JSON
    nlohmann::json default_value = nullptr; // nullptr = no default
    std::vector<std::string> enum_values;
    FieldType array_item_type = FieldType::String; // for Array fields
    std::string ref_schema; // for $ref to another schema component
    nlohmann::json additional_properties; // extra JSON Schema properties (min, max, etc.)

    nlohmann::json to_json_schema() const {
        nlohmann::json schema;

        if (!ref_schema.empty()) {
            schema["$ref"] = "#/components/schemas/" + ref_schema;
            if (!description.empty()) {
                // Use allOf wrapper when adding description to $ref
                nlohmann::json wrapper;
                wrapper["allOf"] = nlohmann::json::array({schema});
                wrapper["description"] = description;
                return wrapper;
            }
            return schema;
        }

        if (type == FieldType::Enum) {
            schema["type"] = "string";
            if (!enum_values.empty()) {
                schema["enum"] = enum_values;
            }
        } else if (type == FieldType::Array) {
            schema["type"] = "array";
            nlohmann::json items;
            items["type"] = field_type_to_string(array_item_type);
            schema["items"] = items;
        } else {
            schema["type"] = field_type_to_string(type);
        }

        if (!description.empty()) {
            schema["description"] = description;
        }

        if (arch_default) {
            schema["x-architecture-default"] = true;
        } else if (!default_value.is_null()) {
            schema["default"] = default_value;
        }

        // Merge additional properties
        if (!additional_properties.is_null() && additional_properties.is_object()) {
            for (auto& [key, val] : additional_properties.items()) {
                schema[key] = val;
            }
        }

        return schema;
    }
};

// Schema descriptor for a complete struct (request or response body)
struct SchemaDescriptor {
    std::string name;
    std::string description;
    std::vector<SchemaField> fields;
    std::string base_schema; // for allOf inheritance

    nlohmann::json to_json_schema() const {
        nlohmann::json properties = nlohmann::json::object();
        std::vector<std::string> required_fields;

        for (const auto& field : fields) {
            properties[field.name] = field.to_json_schema();
            if (field.required) {
                required_fields.push_back(field.name);
            }
        }

        nlohmann::json schema;

        if (!base_schema.empty()) {
            // Inheritance via allOf
            nlohmann::json derived;
            derived["type"] = "object";
            if (!properties.empty()) {
                derived["properties"] = properties;
            }
            if (!required_fields.empty()) {
                derived["required"] = required_fields;
            }

            schema["allOf"] = nlohmann::json::array({
                {{"$ref", "#/components/schemas/" + base_schema}},
                derived
            });
        } else {
            schema["type"] = "object";
            if (!properties.empty()) {
                schema["properties"] = properties;
            }
            if (!required_fields.empty()) {
                schema["required"] = required_fields;
            }
        }

        if (!description.empty()) {
            schema["description"] = description;
        }

        return schema;
    }
};

// Fluent builder for SchemaDescriptor
class SchemaBuilder {
public:
    SchemaBuilder(const std::string& name, const std::string& description = "")
        : desc_{name, description, {}, {}} {}

    SchemaBuilder& inherits(const std::string& base) {
        desc_.base_schema = base;
        return *this;
    }

    SchemaBuilder& required_field(const std::string& name, FieldType type, const std::string& description) {
        desc_.fields.push_back({name, type, description, true, false, nullptr, {}, FieldType::String, "", nullptr});
        return *this;
    }

    SchemaBuilder& optional_field(const std::string& name, FieldType type, const std::string& description,
                                   const nlohmann::json& default_value = nullptr) {
        desc_.fields.push_back({name, type, description, false, false, default_value, {}, FieldType::String, "", nullptr});
        return *this;
    }

    SchemaBuilder& arch_default_field(const std::string& name, FieldType type, const std::string& description) {
        desc_.fields.push_back({name, type, description, false, true, nullptr, {}, FieldType::String, "", nullptr});
        return *this;
    }

    SchemaBuilder& enum_field(const std::string& name, const std::string& description,
                              const std::vector<std::string>& values,
                              const std::string& default_value = "") {
        nlohmann::json def = default_value.empty() ? nlohmann::json(nullptr) : nlohmann::json(default_value);
        desc_.fields.push_back({name, FieldType::Enum, description, false, false, def, values, FieldType::String, "", nullptr});
        return *this;
    }

    SchemaBuilder& arch_default_enum(const std::string& name, const std::string& description,
                                     const std::vector<std::string>& values) {
        desc_.fields.push_back({name, FieldType::Enum, description, false, true, nullptr, values, FieldType::String, "", nullptr});
        return *this;
    }

    SchemaBuilder& required_enum(const std::string& name, const std::string& description,
                                  const std::vector<std::string>& values) {
        desc_.fields.push_back({name, FieldType::Enum, description, true, false, nullptr, values, FieldType::String, "", nullptr});
        return *this;
    }

    SchemaBuilder& array_field(const std::string& name, FieldType item_type, const std::string& description,
                               bool is_required = false) {
        desc_.fields.push_back({name, FieldType::Array, description, is_required, false, nullptr, {}, item_type, "", nullptr});
        return *this;
    }

    SchemaBuilder& ref_field(const std::string& name, const std::string& ref_schema,
                             const std::string& description = "", bool is_required = false) {
        desc_.fields.push_back({name, FieldType::Object, description, is_required, false, nullptr, {}, FieldType::String, ref_schema, nullptr});
        return *this;
    }

    SchemaBuilder& object_field(const std::string& name, const std::string& description,
                                bool is_required = false, const nlohmann::json& default_value = nullptr) {
        desc_.fields.push_back({name, FieldType::Object, description, is_required, false, default_value, {}, FieldType::String, "", nullptr});
        return *this;
    }

    const SchemaDescriptor& build() const { return desc_; }
    SchemaDescriptor& build() { return desc_; }

private:
    SchemaDescriptor desc_;
};

// Trait to check if a type has a static schema() method
template<typename T, typename = void>
struct has_schema : std::false_type {};

template<typename T>
struct has_schema<T, std::void_t<decltype(T::schema())>> : std::true_type {};

template<typename T>
inline constexpr bool has_schema_v = has_schema<T>::value;

} // namespace schema
} // namespace sdcpp
