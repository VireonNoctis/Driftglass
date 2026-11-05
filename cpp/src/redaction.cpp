#include "driftglass/core.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

namespace driftglass {
namespace {

bool has(const Json& value, const char* key) {
    return value.is_object() && value.contains(key);
}

using Tokens = std::map<std::string, std::string>;

std::string add_token(Tokens& tokens, const std::string& value, const std::string& label) {
    const auto found = tokens.find(value);
    if (found != tokens.end()) return found->second;
    std::ostringstream token;
    token << "[REDACTED_" << label << "_" << std::setw(3) << std::setfill('0') << (tokens.size() + 1U) << "]";
    const auto inserted = token.str();
    tokens.emplace(value, inserted);
    return inserted;
}

bool path_start(const std::string& value) {
    if (value.size() > 1U && value[0] == '/' && value[1] != '/') return true;
    return value.size() >= 3U
        && std::isalpha(static_cast<unsigned char>(value[0])) != 0
        && value[1] == ':' && value[2] == '\\';
}

bool path_field(const std::string& key, const std::string& resource_kind) {
    std::string normalized;
    normalized.reserve(key.size());
    for (const char c : key) {
        if (c != '-') normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return normalized.find("path") != std::string::npos
        || normalized == "filename" || normalized == "file_name"
        || (normalized == "identifier" && resource_kind == "file");
}

std::string host_from_identifier(const std::string& value) {
    if (value.empty()) return {};
    const auto scheme = value.find("://");
    if (scheme != std::string::npos) {
        const auto start = scheme + 3U;
        const auto end = value.find_first_of("/:?#", start);
        auto host = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!host.empty() && host.front() == '[') {
            const auto closing = host.find(']');
            if (closing != std::string::npos) host = host.substr(1, closing - 1U);
        }
        return host;
    }
    if (value.front() == '[') {
        const auto closing = value.find(']');
        if (closing != std::string::npos) return value.substr(1, closing - 1U);
    }
    const auto colon = value.rfind(':');
    if (colon != std::string::npos && colon > 0U && colon + 1U < value.size()) {
        const auto port = value.substr(colon + 1U);
        if (std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return value.substr(0, colon);
        }
    }
    return value;
}

void collect_paths(const Json& value, Tokens& paths, const std::string& key = "",
                   const std::string& resource_kind = "") {
    if (value.is_object()) {
        auto kind = resource_kind;
        if (has(value, "kind") && value.at("kind").is_string()) kind = value.at("kind").get<std::string>();
        for (auto it = value.begin(); it != value.end(); ++it) {
            collect_paths(it.value(), paths, it.key(), kind);
        }
    } else if (value.is_array()) {
        for (const auto& child : value) collect_paths(child, paths, key, resource_kind);
    } else if (value.is_string()) {
        const auto candidate = value.get<std::string>();
        if (path_field(key, resource_kind) || path_start(candidate)) add_token(paths, candidate, "PATH");
    }
}

void collect_hosts(const Json& value, Tokens& hosts) {
    if (value.is_object()) {
        const bool network_resource = has(value, "kind") && value.at("kind") == "network";
        if (network_resource) {
            for (const auto& field : {"address", "identifier"}) {
                if (has(value, field) && value.at(field).is_string()) {
                    auto host = field == std::string("address")
                        ? value.at(field).get<std::string>()
                        : host_from_identifier(value.at(field).get<std::string>());
                    if (!host.empty()) add_token(hosts, host, "HOST");
                }
            }
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            std::string key = it.key();
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool host_field = key.find("hostname") != std::string::npos
                || key.find("host") != std::string::npos
                || key.find("address") != std::string::npos
                || key.find("_ip") != std::string::npos
                || key.find("destination") != std::string::npos;
            if (host_field && it.value().is_string() && !it.value().get<std::string>().empty()) {
                const auto host = host_from_identifier(it.value().get<std::string>());
                if (!host.empty()) add_token(hosts, host, "HOST");
            } else {
                collect_hosts(it.value(), hosts);
            }
        }
    } else if (value.is_array()) {
        for (const auto& child : value) collect_hosts(child, hosts);
    }
}

void collect_process_names(const Json& value, Tokens& names, const std::string& key = "",
                           bool process_context = false) {
    if (value.is_object()) {
        const bool inside = process_context || key == "process" || key == "process_identity";
        for (auto it = value.begin(); it != value.end(); ++it) {
            const auto normalized = it.key();
            const bool executable = normalized == "process_executable"
                || (inside && normalized == "executable");
            if (executable && it.value().is_string() && !it.value().get<std::string>().empty()) {
                add_token(names, it.value().get<std::string>(), "PROCESS");
            } else {
                collect_process_names(it.value(), names, normalized, inside);
            }
        }
    } else if (value.is_array()) {
        for (const auto& child : value) collect_process_names(child, names, key, process_context);
    }
}

bool boundary_character(char value) {
    const auto c = static_cast<unsigned char>(value);
    return std::isalnum(c) != 0 || value == '_' || value == '.' || value == '-';
}

std::vector<std::pair<std::string, std::string>> longest_first(const Tokens& tokens) {
    std::vector<std::pair<std::string, std::string>> result(tokens.begin(), tokens.end());
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.first.size() != right.first.size()) return left.first.size() > right.first.size();
        return left.first < right.first;
    });
    return result;
}

std::string replace_bounded(std::string value, const Tokens& tokens) {
    for (const auto& [original, token] : longest_first(tokens)) {
        if (original.empty()) continue;
        std::size_t position = 0;
        while ((position = value.find(original, position)) != std::string::npos) {
            const bool left = position == 0U || !boundary_character(value[position - 1U]);
            const auto end = position + original.size();
            const bool right = end >= value.size() || !boundary_character(value[end]);
            if (left && right) {
                value.replace(position, original.size(), token);
                position += token.size();
            } else {
                position += original.size();
            }
        }
    }
    return value;
}

bool path_delimiter(char c) {
    const auto value = static_cast<unsigned char>(c);
    return std::isspace(value) != 0 || c == '<' || c == '>' || c == '"' || c == '\'' || c == '|';
}

std::string replace_embedded_paths(std::string value, Tokens& paths) {
    for (const auto& [original, token] : longest_first(paths)) {
        std::size_t start = 0;
        while ((start = value.find(original, start)) != std::string::npos) {
            value.replace(start, original.size(), token);
            start += token.size();
        }
    }

    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool unix_start = value[index] == '/'
            && (index == 0U || (!boundary_character(value[index - 1U]) && value[index - 1U] != ':' && value[index - 1U] != '/'))
            && index + 1U < value.size() && value[index + 1U] != '/';
        const bool windows_start = index + 2U < value.size()
            && std::isalpha(static_cast<unsigned char>(value[index])) != 0
            && value[index + 1U] == ':' && value[index + 2U] == '\\'
            && (index == 0U || !boundary_character(value[index - 1U]));
        if (!unix_start && !windows_start) continue;
        std::size_t end = index;
        while (end < value.size() && !path_delimiter(value[end])) ++end;
        while (end > index && std::string(".,;:)]}").find(value[end - 1U]) != std::string::npos) --end;
        if (end <= index + 1U) continue;
        const auto raw = value.substr(index, end - index);
        const auto token = add_token(paths, raw, "PATH");
        value.replace(index, end - index, token);
        index += token.size() - 1U;
    }
    return value;
}

std::string lower_key(std::string key) {
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(key.begin(), key.end(), '-', '_');
    return key;
}

Json redact_value(const Json& value, Tokens& paths, Tokens& hosts, const Tokens& process_names,
                  bool redact_paths, bool redact_network, bool redact_process,
                  const std::string& key = "", const std::string& resource_kind = "",
                  bool process_context = false) {
    if (value.is_object()) {
        auto kind = resource_kind;
        if (has(value, "kind") && value.at("kind").is_string()) kind = value.at("kind").get<std::string>();
        const bool inside = process_context || key == "process" || key == "process_identity";
        Json result = Json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            result[it.key()] = redact_value(it.value(), paths, hosts, process_names,
                redact_paths, redact_network, redact_process, it.key(), kind, inside);
        }
        return result;
    }
    if (value.is_array()) {
        Json result = Json::array();
        for (const auto& child : value) {
            result.push_back(redact_value(child, paths, hosts, process_names,
                redact_paths, redact_network, redact_process, key, resource_kind, process_context));
        }
        return result;
    }
    if (!value.is_string() || value.get<std::string>().empty()) return value;

    std::string result = value.get<std::string>();
    const auto normalized = lower_key(key);
    if (redact_paths) {
        if (path_field(key, resource_kind) || path_start(result)) return add_token(paths, result, "PATH");
        result = replace_embedded_paths(std::move(result), paths);
    }

    if (redact_network) {
        if (resource_kind == "network" && normalized == "address") {
            const auto found = hosts.find(result);
            return found == hosts.end() ? Json(add_token(hosts, result, "HOST")) : Json(found->second);
        }
        if (resource_kind == "network" && normalized == "identifier") {
            result = replace_bounded(std::move(result), hosts);
        } else {
            result = replace_bounded(std::move(result), hosts);
        }
    }

    if (redact_process) {
        const bool executable = normalized == "process_executable" || (process_context && normalized == "executable");
        if (executable) {
            const auto found = process_names.find(result);
            if (found != process_names.end()) return found->second;
        }
        result = replace_bounded(std::move(result), process_names);
    }
    return result;
}

} // namespace

Json redact_report(const Json& report, bool redact_paths, bool redact_network, bool redact_process_names) {
    if (!redact_paths && !redact_network && !redact_process_names) {
        throw Error("select at least one redaction category");
    }
    validate_report(report);
    Tokens paths;
    Tokens hosts;
    Tokens processes;
    if (redact_paths) collect_paths(report, paths);
    if (redact_network) collect_hosts(report, hosts);
    if (redact_process_names) collect_process_names(report, processes);

    Json result = redact_value(report, paths, hosts, processes,
        redact_paths, redact_network, redact_process_names);
    for (auto& finding : result["findings"]) {
        Json identity = finding;
        identity.erase("finding_id");
        identity.erase("interpretation");
        finding["finding_id"] = sha256_hex(canonical_json(identity));
    }
    Json identity = result;
    identity.erase("report_id");
    result["report_id"] = sha256_hex(canonical_json(identity));
    validate_report(result);
    return result;
}

} // namespace driftglass
