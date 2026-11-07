#include "driftglass/core.hpp"

#include <cmath>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace driftglass {
namespace {

class SchemaIssue final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string child_path(const std::string& parent, const std::string& child) {
    return parent + "." + child;
}

std::size_t utf8_length(const std::string& value) {
    std::size_t length = 0;
    for (const unsigned char byte : value) {
        if ((byte & 0xc0U) != 0x80U) ++length;
    }
    return length;
}

bool type_matches(const Json& value, const std::string& type) {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    if (type == "integer") {
        if (value.is_number_integer() || value.is_number_unsigned()) return true;
        return value.is_number_float() && std::isfinite(value.get<double>())
            && std::floor(value.get<double>()) == value.get<double>();
    }
    if (type == "number") return value.is_number();
    throw SchemaIssue("unsupported JSON Schema type '" + type + "'");
}

std::vector<std::string> reference_tokens(const std::string& reference) {
    if (reference == "#") return {};
    if (!reference.starts_with("#/")) {
        throw SchemaIssue("only local JSON Schema references are supported");
    }

    std::vector<std::string> tokens;
    std::size_t start = 2;
    while (start <= reference.size()) {
        const auto end = reference.find('/', start);
        std::string token = reference.substr(start, end == std::string::npos
            ? std::string::npos : end - start);
        std::string decoded;
        for (std::size_t index = 0; index < token.size(); ++index) {
            if (token[index] == '~' && index + 1 < token.size()) {
                const char escaped = token[++index];
                if (escaped == '0') decoded.push_back('~');
                else if (escaped == '1') decoded.push_back('/');
                else throw SchemaIssue("invalid JSON Pointer escape in schema reference");
            } else {
                decoded.push_back(token[index]);
            }
        }
        tokens.push_back(std::move(decoded));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return tokens;
}

const Json& resolve_reference(const Json& root, const std::string& reference) {
    const Json* current = &root;
    for (const auto& token : reference_tokens(reference)) {
        if (current->is_object() && current->contains(token)) {
            current = &current->at(token);
        } else {
            throw SchemaIssue("JSON Schema reference does not resolve: " + reference);
        }
    }
    return *current;
}

const std::set<std::string>& supported_keywords() {
    static const std::set<std::string> keywords{
        "$schema", "$id", "$defs", "$ref", "title", "description", "default",
        "examples", "$comment", "deprecated", "readOnly", "writeOnly",
        "type", "const", "enum", "required", "properties", "additionalProperties",
        "items", "minItems", "maxItems", "uniqueItems", "minProperties",
        "maxProperties", "minLength", "maxLength", "pattern", "propertyNames",
        "minimum", "maximum", "format", "anyOf", "oneOf", "allOf", "if", "then"
    };
    return keywords;
}

void check_schema_keywords(const Json& schema, std::size_t depth) {
    if (depth > 128U) throw SchemaIssue("JSON Schema nesting exceeds the supported limit");
    if (schema.is_boolean()) return;
    if (!schema.is_object()) throw SchemaIssue("a JSON Schema must be an object or boolean");

    for (auto it = schema.begin(); it != schema.end(); ++it) {
        if (!supported_keywords().contains(it.key())) {
            throw SchemaIssue("unsupported JSON Schema keyword '" + it.key() + "'");
        }
        const auto& key = it.key();
        const auto& value = it.value();
        if (key == "$defs" || key == "properties") {
            if (!value.is_object()) throw SchemaIssue("JSON Schema '" + key + "' must be an object");
            for (auto child = value.begin(); child != value.end(); ++child) {
                check_schema_keywords(child.value(), depth + 1U);
            }
        } else if (key == "items" || key == "additionalProperties" || key == "propertyNames"
            || key == "if" || key == "then") {
            if (value.is_object() || value.is_boolean()) check_schema_keywords(value, depth + 1U);
            else if (key != "additionalProperties" && key != "items") {
                throw SchemaIssue("JSON Schema '" + key + "' must be an object or boolean");
            }
        } else if (key == "anyOf" || key == "oneOf" || key == "allOf") {
            if (!value.is_array() || value.empty()) {
                throw SchemaIssue("JSON Schema '" + key + "' must be a non-empty array");
            }
            for (const auto& child : value) check_schema_keywords(child, depth + 1U);
        }
    }
}

void validate_node(const Json& instance, const Json& schema, const Json& root,
                   const std::string& path, std::size_t depth);

bool matches_schema(const Json& instance, const Json& schema, const Json& root,
                    const std::string& path, std::size_t depth) {
    try {
        validate_node(instance, schema, root, path, depth + 1U);
        return true;
    } catch (const SchemaIssue&) {
        return false;
    }
}

void validate_node(const Json& instance, const Json& schema, const Json& root,
                   const std::string& path, std::size_t depth) {
    if (depth > 128U) throw SchemaIssue(path + ": schema evaluation exceeds the nesting limit");
    if (schema.is_boolean()) {
        if (!schema.get<bool>()) throw SchemaIssue(path + ": value is rejected by a false schema");
        return;
    }
    if (!schema.is_object()) throw SchemaIssue(path + ": schema node must be an object or boolean");

    if (schema.contains("$ref")) {
        if (!schema.at("$ref").is_string()) throw SchemaIssue(path + ": schema $ref must be a string");
        validate_node(instance, resolve_reference(root, schema.at("$ref").get<std::string>()),
                      root, path, depth + 1U);
    }

    if (schema.contains("type")) {
        const auto& type = schema.at("type");
        bool matched = false;
        if (type.is_string()) {
            matched = type_matches(instance, type.get<std::string>());
        } else if (type.is_array() && !type.empty()) {
            for (const auto& item : type) {
                if (!item.is_string()) throw SchemaIssue(path + ": schema type entries must be strings");
                matched = matched || type_matches(instance, item.get<std::string>());
            }
        } else {
            throw SchemaIssue(path + ": schema type must be a string or non-empty array");
        }
        if (!matched) throw SchemaIssue(path + ": value has the wrong JSON type");
    }

    if (schema.contains("const") && instance != schema.at("const")) {
        throw SchemaIssue(path + ": value does not match schema const");
    }
    if (schema.contains("enum")) {
        const auto& values = schema.at("enum");
        if (!values.is_array() || values.empty()) throw SchemaIssue(path + ": schema enum must be a non-empty array");
        if (std::find(values.begin(), values.end(), instance) == values.end()) {
            throw SchemaIssue(path + ": value is not one of the allowed enum values");
        }
    }

    if (instance.is_string()) {
        const auto value = instance.get<std::string>();
        const auto length = utf8_length(value);
        if (schema.contains("minLength") && length < schema.at("minLength").get<std::size_t>()) {
            throw SchemaIssue(path + ": string is shorter than minLength");
        }
        if (schema.contains("maxLength") && length > schema.at("maxLength").get<std::size_t>()) {
            throw SchemaIssue(path + ": string exceeds maxLength");
        }
        if (schema.contains("pattern")) {
            try {
                if (!std::regex_search(value, std::regex(schema.at("pattern").get<std::string>()))) {
                    throw SchemaIssue(path + ": string does not match pattern");
                }
            } catch (const std::regex_error&) {
                throw SchemaIssue(path + ": schema pattern is not a valid regular expression");
            }
        }
        if (schema.contains("format") && schema.at("format") == "date-time") {
            static const std::regex date_time(
                R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})$)");
            if (!std::regex_match(value, date_time)) {
                throw SchemaIssue(path + ": string is not an RFC 3339 date-time");
            }
        }
    }

    if (instance.is_number()) {
        const double value = instance.get<double>();
        if (schema.contains("minimum") && value < schema.at("minimum").get<double>()) {
            throw SchemaIssue(path + ": number is below minimum");
        }
        if (schema.contains("maximum") && value > schema.at("maximum").get<double>()) {
            throw SchemaIssue(path + ": number exceeds maximum");
        }
    }

    if (instance.is_object()) {
        if (schema.contains("minProperties")
            && instance.size() < schema.at("minProperties").get<std::size_t>()) {
            throw SchemaIssue(path + ": object has fewer than minProperties");
        }
        if (schema.contains("maxProperties")
            && instance.size() > schema.at("maxProperties").get<std::size_t>()) {
            throw SchemaIssue(path + ": object exceeds maxProperties");
        }

        if (schema.contains("required")) {
            const auto& required = schema.at("required");
            if (!required.is_array()) throw SchemaIssue(path + ": schema required must be an array");
            for (const auto& name : required) {
                if (!name.is_string()) throw SchemaIssue(path + ": schema required entries must be strings");
                const auto key = name.get<std::string>();
                if (!instance.contains(key)) throw SchemaIssue(child_path(path, key) + ": required property is missing");
            }
        }

        const Json properties = schema.value("properties", Json::object());
        if (!properties.is_object()) throw SchemaIssue(path + ": schema properties must be an object");
        if (schema.contains("propertyNames")) {
            for (auto it = instance.begin(); it != instance.end(); ++it) {
                validate_node(it.key(), schema.at("propertyNames"), root,
                              child_path(path, "<property-name>"), depth + 1U);
            }
        }
        for (auto it = instance.begin(); it != instance.end(); ++it) {
            if (properties.contains(it.key())) {
                validate_node(it.value(), properties.at(it.key()), root,
                              child_path(path, it.key()), depth + 1U);
            } else if (schema.contains("additionalProperties")) {
                const auto& additional = schema.at("additionalProperties");
                if (additional.is_boolean()) {
                    if (!additional.get<bool>()) {
                        throw SchemaIssue(child_path(path, it.key()) + ": additional property is not allowed");
                    }
                } else {
                    validate_node(it.value(), additional, root, child_path(path, it.key()), depth + 1U);
                }
            }
        }
    }

    if (instance.is_array()) {
        if (schema.contains("minItems") && instance.size() < schema.at("minItems").get<std::size_t>()) {
            throw SchemaIssue(path + ": array has fewer than minItems");
        }
        if (schema.contains("maxItems") && instance.size() > schema.at("maxItems").get<std::size_t>()) {
            throw SchemaIssue(path + ": array exceeds maxItems");
        }
        if (schema.contains("uniqueItems") && schema.at("uniqueItems").is_boolean()
            && schema.at("uniqueItems").get<bool>()) {
            std::set<std::string> values;
            for (const auto& value : instance) {
                if (!values.insert(canonical_json(value)).second) {
                    throw SchemaIssue(path + ": array items must be unique");
                }
            }
        }
        if (schema.contains("items")) {
            std::size_t index = 0;
            for (const auto& value : instance) {
                validate_node(value, schema.at("items"), root,
                              path + "[" + std::to_string(index++) + "]", depth + 1U);
            }
        }
    }

    for (const auto* keyword : {"anyOf", "oneOf", "allOf"}) {
        if (!schema.contains(keyword)) continue;
        const auto& choices = schema.at(keyword);
        std::size_t matches = 0;
        for (const auto& candidate : choices) {
            if (matches_schema(instance, candidate, root, path, depth)) ++matches;
        }
        if (std::string(keyword) == "anyOf" && matches == 0U) {
            throw SchemaIssue(path + ": value does not match anyOf branches");
        }
        if (std::string(keyword) == "oneOf" && matches != 1U) {
            throw SchemaIssue(path + ": value must match exactly one oneOf branch");
        }
        if (std::string(keyword) == "allOf" && matches != choices.size()) {
            throw SchemaIssue(path + ": value does not match every allOf branch");
        }
    }

    if (schema.contains("if")) {
        if (matches_schema(instance, schema.at("if"), root, path, depth) && schema.contains("then")) {
            validate_node(instance, schema.at("then"), root, path, depth + 1U);
        }
    }
}

} // namespace

void validate_json_schema(const Json& instance, const Json& schema) {
    try {
        check_schema_keywords(schema, 0U);
        validate_node(instance, schema, schema, "$", 0U);
    } catch (const SchemaIssue& error) {
        throw Error(std::string("JSON Schema validation failed: ") + error.what());
    } catch (const Json::exception&) {
        throw Error("JSON Schema contains a keyword with an invalid value or type");
    }
}

void validate_json_schema_file(const Json& instance, const std::filesystem::path& schema_path) {
    if (schema_path.empty()) return;
    const auto schema = load_json_file(schema_path, 4U * 1024U * 1024U);
    validate_json_schema(instance, schema);
}

} // namespace driftglass
