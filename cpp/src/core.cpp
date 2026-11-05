#include "driftglass/core.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <openssl/evp.h>

namespace driftglass {
namespace {

constexpr std::size_t kManifestLimit = 4U * 1024U * 1024U;
constexpr std::size_t kEventStreamLimit = 64U * 1024U * 1024U;
constexpr std::size_t kEventLineLimit = 1024U * 1024U;
constexpr std::size_t kEventCountLimit = 250000U;

const std::set<std::string> kEventCategories{
    "process", "network", "file", "system", "coverage"
};
const std::set<std::string> kCoverageCategories{
    "process", "network", "file", "system", "persistence"
};
const std::set<std::string> kFindingCategories{
    "process", "network", "file", "system"
};

bool has(const Json& object, const char* key) {
    return object.is_object() && object.contains(key);
}

void require_object(const Json& value, const std::string& label) {
    if (!value.is_object()) throw Error(label + " must be an object");
}

void require_array(const Json& value, const std::string& label) {
    if (!value.is_array()) throw Error(label + " must be an array");
}

void allowed_keys(
    const Json& object,
    const std::set<std::string>& allowed,
    const std::string& label) {
    require_object(object, label);
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!allowed.contains(it.key())) {
            throw Error(label + " contains an unsupported field '" + it.key() + "'");
        }
    }
}

const Json& required(const Json& object, const char* key, const std::string& label) {
    if (!has(object, key)) throw Error(label + " is missing required field '" + key + "'");
    return object.at(key);
}

std::string string_value(const Json& value, const std::string& label,
                         std::size_t minimum = 0, std::size_t maximum = static_cast<std::size_t>(-1)) {
    if (!value.is_string()) throw Error(label + " must be a string");
    const auto& result = value.get_ref<const std::string&>();
    if (result.size() < minimum) throw Error(label + " is shorter than allowed");
    if (result.size() > maximum) throw Error(label + " exceeds its length limit");
    return result;
}

bool integer_value(const Json& value) {
    return value.is_number_integer() || value.is_number_unsigned();
}

std::int64_t nonnegative_integer(const Json& value, const std::string& label) {
    if (!integer_value(value)) throw Error(label + " must be an integer");
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(INT64_MAX)) throw Error(label + " is out of range");
        return static_cast<std::int64_t>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < 0) throw Error(label + " must be non-negative");
    return number;
}

void enum_value(const Json& value, const std::set<std::string>& choices, const std::string& label) {
    const auto text = string_value(value, label, 1);
    if (!choices.contains(text)) throw Error(label + " is not in the allowed set");
}

std::string lower_hex_digest(const Json& value, const std::string& label) {
    const auto text = string_value(value, label, 64, 64);
    if (!std::regex_match(text, std::regex("^[a-f0-9]{64}$"))) {
        throw Error(label + " must be a lowercase SHA-256 digest");
    }
    return text;
}

std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> parse_timestamp(const std::string& input) {
    static const std::regex pattern(
        R"(^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.([0-9]+))?(Z|[+-][0-9]{2}:[0-9]{2})$)");
    std::smatch match;
    if (!std::regex_match(input, match, pattern)) return std::nullopt;

    const int year = std::stoi(match[1].str());
    const unsigned month = static_cast<unsigned>(std::stoi(match[2].str()));
    const unsigned day = static_cast<unsigned>(std::stoi(match[3].str()));
    const int hour = std::stoi(match[4].str());
    const int minute = std::stoi(match[5].str());
    const int second = std::stoi(match[6].str());
    const std::chrono::year_month_day date{
        std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}
    };
    if (!date.ok() || hour > 23 || minute > 59 || second > 59) return std::nullopt;

    std::chrono::nanoseconds fraction{0};
    const std::string fraction_text = match[7].str();
    if (!fraction_text.empty()) {
        std::string digits = fraction_text.substr(0, 9);
        digits.append(9U - digits.size(), '0');
        fraction = std::chrono::nanoseconds{std::stoll(digits)};
    }

    int offset_minutes = 0;
    const std::string zone = match[8].str();
    if (zone != "Z") {
        const int offset_hour = std::stoi(zone.substr(1, 2));
        const int offset_minute = std::stoi(zone.substr(4, 2));
        if (offset_hour > 23 || offset_minute > 59) return std::nullopt;
        offset_minutes = offset_hour * 60 + offset_minute;
        if (zone[0] == '-') offset_minutes = -offset_minutes;
    }

    const auto local = std::chrono::sys_days{date}
        + std::chrono::hours{hour}
        + std::chrono::minutes{minute}
        + std::chrono::seconds{second}
        + fraction;
    return local - std::chrono::minutes{offset_minutes};
}

void validate_timestamp(const Json& value, const std::string& label) {
    const auto timestamp = string_value(value, label, 1, 64);
    if (!parse_timestamp(timestamp)) throw Error(label + " must be a date-time with an explicit UTC offset");
}

void validate_attribute_values(const Json& attributes) {
    require_object(attributes, "event.attributes");
    if (attributes.size() > 64U) throw Error("event.attributes exceeds its 64-property limit");
    static const std::regex key_pattern("^[A-Za-z][A-Za-z0-9_.-]{0,63}$");
    for (auto it = attributes.begin(); it != attributes.end(); ++it) {
        if (!std::regex_match(it.key(), key_pattern)) {
            throw Error("event.attributes contains an invalid property name");
        }
        const auto& value = it.value();
        if (value.is_string()) {
            string_value(value, "event.attributes." + it.key(), 0, 2048);
        } else if (!(value.is_null() || value.is_boolean() || value.is_number())) {
            throw Error("event.attributes values must be bounded scalar JSON values");
        }
    }
}

void validate_event_process(const Json& process) {
    allowed_keys(process, {"pid", "parent_pid", "executable", "command_line"}, "event.process");
    if (has(process, "pid")) nonnegative_integer(process["pid"], "event.process.pid");
    if (has(process, "parent_pid")) nonnegative_integer(process["parent_pid"], "event.process.parent_pid");
    if (has(process, "executable")) string_value(process["executable"], "event.process.executable", 0, 4096);
    if (has(process, "command_line")) string_value(process["command_line"], "event.process.command_line", 0, 8192);
}

void validate_event_resource(const Json& resource) {
    allowed_keys(resource, {"kind", "identifier", "address", "port", "protocol"}, "event.resource");
    if (has(resource, "kind")) enum_value(resource["kind"], {"process", "file", "network", "system", "unknown"}, "event.resource.kind");
    if (has(resource, "identifier")) string_value(resource["identifier"], "event.resource.identifier", 0, 4096);
    if (has(resource, "address")) string_value(resource["address"], "event.resource.address", 0, 253);
    if (has(resource, "port")) {
        const auto port = nonnegative_integer(resource["port"], "event.resource.port");
        if (port > 65535) throw Error("event.resource.port exceeds 65535");
    }
    if (has(resource, "protocol")) string_value(resource["protocol"], "event.resource.protocol", 0, 32);
}

std::set<std::string> string_set(const Json& values, const std::string& label,
                                 const std::set<std::string>& allowed,
                                 bool require_one = false) {
    require_array(values, label);
    if (require_one && values.empty()) throw Error(label + " must contain at least one value");
    std::set<std::string> result;
    for (const auto& value : values) {
        const auto item = string_value(value, label + " item", 1, 128);
        if (!allowed.contains(item)) throw Error(label + " contains an unsupported value");
        if (!result.insert(item).second) throw Error(label + " must not contain duplicates");
    }
    return result;
}

Json details(const Json& event, const RuleProfile& profile, bool include_identity) {
    Json process_executable = nullptr;
    if (has(event, "process") && has(event["process"], "executable")) {
        process_executable = event["process"]["executable"];
    }

    Json attributes = Json::object();
    if (has(event, "attributes") && event["attributes"].is_object()) {
        for (auto it = event["attributes"].begin(); it != event["attributes"].end(); ++it) {
            if (it.key() == "process_start_time_ticks") continue;
            if (std::find(profile.ignored_attributes.begin(), profile.ignored_attributes.end(), it.key()) != profile.ignored_attributes.end()) continue;
            attributes[it.key()] = it.value();
        }
    }

    Json result{
        {"category", event.at("category")},
        {"action", event.at("action")},
        {"process_executable", process_executable},
        {"resource", has(event, "resource") ? event["resource"] : Json(nullptr)},
        {"outcome", has(event, "outcome") ? event["outcome"] : Json(nullptr)},
        {"attributes", attributes}
    };

    if (include_identity && has(event, "process") && event["process"].is_object()) {
        const auto& process = event["process"];
        if (has(process, "pid") && has(process, "parent_pid") && has(process, "executable")
            && integer_value(process["pid"]) && integer_value(process["parent_pid"])
            && process["executable"].is_string()) {
            const auto pid = nonnegative_integer(process["pid"], "event.process.pid");
            const auto parent = nonnegative_integer(process["parent_pid"], "event.process.parent_pid");
            Json identity{
                {"pid", pid},
                {"parent_pid", parent},
                {"executable", process["executable"]}
            };
            if (has(event, "attributes") && has(event["attributes"], "process_start_time_ticks")
                && integer_value(event["attributes"]["process_start_time_ticks"])) {
                const auto start = nonnegative_integer(event["attributes"]["process_start_time_ticks"], "event.attributes.process_start_time_ticks");
                identity["start_time_ticks"] = start;
            }
            result["process_identity"] = identity;
        }
    }
    return result;
}

std::string identity(const Json& event) {
    const Json resource = has(event, "resource") && event["resource"].is_object()
        ? event["resource"] : Json::object();
    Json identifier = has(resource, "identifier") ? resource["identifier"] : Json(nullptr);
    if (event["category"] == "network" && resource.value("kind", std::string{}) == "network") identifier = nullptr;
    Json executable = nullptr;
    if (has(event, "process") && has(event["process"], "executable")) executable = event["process"]["executable"];
    return canonical_json(Json{
        {"category", event["category"]},
        {"action", event["action"]},
        {"process_executable", executable},
        {"resource_kind", has(resource, "kind") ? resource["kind"] : Json(nullptr)},
        {"resource_identifier", identifier},
        {"protocol", has(resource, "protocol") ? resource["protocol"] : Json(nullptr)}
    });
}

std::string signature(const Json& event, const RuleProfile& profile) {
    Json result = details(event, profile, false);
    if (event["category"] == "network" && result["resource"].is_object()
        && result["resource"].value("kind", std::string{}) == "network") {
        result["resource"].erase("identifier");
        result["attributes"].erase("socket_inode");
        result["attributes"].erase("local_port");
    }
    return canonical_json(result);
}

using EventOrder = std::tuple<std::string, std::string, std::string>;
EventOrder event_order(const Json& event, const RuleProfile& profile) {
    return {
        signature(event, profile),
        event.at("event_id").get<std::string>(),
        event.at("timestamp").get<std::string>()
    };
}

Json evidence(const std::string& run_id, const Json& event, const RuleProfile& profile) {
    RuleProfile evidence_profile = profile;
    evidence_profile.ignored_attributes.clear();
    return Json{
        {"run_id", run_id},
        {"event_id", event.at("event_id")},
        {"collector", event.at("source").at("collector")},
        {"source_event_ref", event.at("source").at("event_ref")},
        {"timestamp", event.at("timestamp")},
        {"observed", details(event, evidence_profile, true)}
    };
}

Json uncertainty(std::string code, std::string message, Json run_id = nullptr) {
    return Json{{"code", std::move(code)}, {"message", std::move(message)}, {"run_id", std::move(run_id)}};
}

std::vector<Json> coverage_uncertainties(const Json& manifest, const std::vector<Json>& events) {
    const auto run_id = manifest.at("run_id");
    const auto& capture = manifest.at("capture");
    const auto& coverage = manifest.at("coverage");
    std::vector<Json> found;

    if (capture.at("status") != "completed") {
        found.push_back(uncertainty(
            "capture_not_completed",
            "Capture status is '" + capture.at("status").get<std::string>() + "'; observations may be incomplete.",
            run_id));
    }

    std::set<std::string> unavailable;
    for (const auto& item : coverage.at("unavailable_categories")) unavailable.insert(item.get<std::string>());
    if (!unavailable.empty()) {
        std::string joined;
        for (const auto& item : unavailable) joined += (joined.empty() ? "" : ", ") + item;
        found.push_back(uncertainty("categories_unavailable", "Unavailable event categories: " + joined + ".", run_id));
    }

    std::size_t inactive_count = 0;
    for (const auto& source : coverage.at("event_sources")) {
        if (source.at("state") != "active") ++inactive_count;
    }
    if (inactive_count > 0) {
        found.push_back(uncertainty(
            "event_sources_not_active",
            std::to_string(inactive_count) + " configured event source(s) were not active.",
            run_id));
    }

    const auto dropped = coverage.at("dropped_events").get<std::uint64_t>();
    if (dropped > 0) {
        found.push_back(uncertainty(
            "events_dropped",
            "Collector reports " + std::to_string(dropped) + " dropped event(s).",
            run_id));
    }

    if (!coverage.at("errors").empty()) {
        found.push_back(uncertainty(
            "collection_errors",
            "Run manifest records " + std::to_string(coverage.at("errors").size()) + " collection error(s); inspect the local manifest for details.",
            run_id));
    }

    std::set<std::string> active_categories;
    for (const auto& source : coverage.at("event_sources")) {
        if (source.at("state") != "active") continue;
        for (const auto& category : source.at("categories")) active_categories.insert(category.get<std::string>());
    }
    std::set<std::string> undeclared;
    for (const auto& category : kCoverageCategories) {
        if (!active_categories.contains(category) && !unavailable.contains(category)) undeclared.insert(category);
    }
    if (!undeclared.empty()) {
        std::string joined;
        for (const auto& item : undeclared) joined += (joined.empty() ? "" : ", ") + item;
        found.push_back(uncertainty(
            "coverage_not_declared",
            "Coverage was not declared active or unavailable for: " + joined + ".",
            run_id));
    }

    std::map<std::string, std::size_t> quality_counts;
    std::set<std::string> coverage_actions;
    for (const auto& event : events) {
        if (event.at("collection_quality") != "complete") {
            ++quality_counts[event.at("collection_quality").get<std::string>()];
        }
        if (event.at("category") == "coverage") {
            const auto action = event.at("action").get<std::string>();
            if (action == "coverage_gap" || action == "capture_incomplete"
                || action == "collector_error" || action == "events_dropped") {
                coverage_actions.insert(action);
            }
        }
    }
    if (!quality_counts.empty()) {
        std::string detail_text;
        for (const auto& [quality, count] : quality_counts) {
            detail_text += (detail_text.empty() ? "" : ", ") + quality + ": " + std::to_string(count);
        }
        found.push_back(uncertainty(
            "events_with_reduced_quality",
            "Input contains event records with non-complete collection quality (" + detail_text + ").",
            run_id));
    }
    if (!coverage_actions.empty()) {
        std::string joined;
        for (const auto& item : coverage_actions) joined += (joined.empty() ? "" : ", ") + item;
        found.push_back(uncertainty("coverage_events_present", "Coverage events report: " + joined + ".", run_id));
    }

    if (manifest.at("application").at("artifact_sha256").is_null()) {
        found.push_back(uncertainty("artifact_hash_missing", "Application artifact SHA-256 was unavailable for this run.", run_id));
    }
    return found;
}

void validate_pair(const Json& baseline, const Json& candidate) {
    if (baseline.at("role") != "baseline") throw Error("first run manifest must have role 'baseline'");
    if (candidate.at("role") != "candidate") throw Error("second run manifest must have role 'candidate'");
    if (baseline.at("experiment_id") != candidate.at("experiment_id")) throw Error("run manifests must share the same experiment_id");
    if (baseline.at("run_id") == candidate.at("run_id")) throw Error("baseline and candidate must have different run_id values");

    const auto& baseline_app = baseline.at("application");
    const auto& candidate_app = candidate.at("application");
    if (baseline_app.at("name") != candidate_app.at("name")) throw Error("run manifests identify different application names");
    if (has(baseline_app, "package_id") && has(candidate_app, "package_id")
        && !baseline_app.at("package_id").is_null() && !candidate_app.at("package_id").is_null()
        && baseline_app.at("package_id") != candidate_app.at("package_id")) {
        throw Error("run manifests identify different application package IDs");
    }
    if (baseline.at("capture").at("status") == "running") throw Error("baseline capture is still running; compare only after capture has stopped");
    if (candidate.at("capture").at("status") == "running") throw Error("candidate capture is still running; compare only after capture has stopped");
}

Json context_differences(const Json& baseline, const Json& candidate) {
    const std::array<std::tuple<const char*, const char*, const char*>, 6> fields{{
        {"environment.os", "environment", "os"},
        {"environment.os_version", "environment", "os_version"},
        {"environment.architecture", "environment", "architecture"},
        {"environment.snapshot_id", "environment", "snapshot_id"},
        {"capture.scenario", "capture", "scenario"},
        {"capture.profile", "capture", "profile"}
    }};
    Json result = Json::array();
    for (const auto& [name, section, key] : fields) {
        const Json& left = baseline.at(section).at(key);
        const Json& right = candidate.at(section).at(key);
        if (left != right) result.push_back(Json{
            {"field", name},
            {"baseline_value", left},
            {"candidate_value", right}
        });
    }
    return result;
}

Json make_finding(
    const std::string& change_type,
    const std::string& category,
    const std::string& baseline_run_id,
    const std::string& candidate_run_id,
    const std::vector<Json>& before,
    const std::vector<Json>& after,
    const RuleProfile& profile) {
    const Json& representative = after.empty() ? before.front() : after.front();
    std::string subject = category + " action '" + representative.at("action").get<std::string>() + "'";
    if (has(representative, "process") && has(representative["process"], "executable")
        && representative["process"]["executable"].is_string()
        && !representative["process"]["executable"].get<std::string>().empty()) {
        subject += " for process '" + representative["process"]["executable"].get<std::string>() + "'";
    }

    std::string summary;
    std::string observation;
    if (change_type == "added") {
        summary = "New candidate observation: " + subject + ".";
        observation = "This observation appears in the candidate input and has no matching baseline record.";
    } else if (change_type == "removed") {
        summary = "Baseline observation not repeated: " + subject + ".";
        observation = "This observation appears in the baseline input but has no matching candidate record in this run.";
    } else {
        summary = "Observation details changed: " + subject + ".";
        observation = "Baseline and candidate observations share the comparison identity but differ in normalized details.";
    }

    Json baseline_evidence = Json::array();
    for (const auto& event : before) baseline_evidence.push_back(evidence(baseline_run_id, event, profile));
    Json candidate_evidence = Json::array();
    for (const auto& event : after) candidate_evidence.push_back(evidence(candidate_run_id, event, profile));

    Json payload{
        {"rule_id", change_type == "changed" ? "DG-COMPARE-002" : "DG-COMPARE-001"},
        {"change_type", change_type},
        {"category", category},
        {"summary", summary},
        {"observation", observation},
        {"baseline_evidence", baseline_evidence},
        {"candidate_evidence", candidate_evidence}
    };
    const std::string finding_id = sha256_hex(canonical_json(payload));
    payload["finding_id"] = finding_id;
    payload["interpretation"] =
        "A behavioural difference is a lead for review, not proof of intent. "
        "An observation not repeated in another run can reflect timing, environment, or collection coverage.";
    return payload;
}

Json coverage_summary(const Json& manifest, const std::vector<Json>& events) {
    const auto& coverage = manifest.at("coverage");
    Json counts{
        {"process", 0}, {"network", 0}, {"file", 0}, {"system", 0}, {"coverage", 0}
    };
    for (const auto& event : events) {
        const auto category = event.at("category").get<std::string>();
        if (counts.contains(category)) ++counts[category];
    }

    Json sources = coverage.at("event_sources");
    for (auto& source : sources) {
        std::sort(source["categories"].begin(), source["categories"].end(),
            [](const Json& left, const Json& right) { return left.get<std::string>() < right.get<std::string>(); });
    }
    std::sort(sources.begin(), sources.end(), [](const Json& left, const Json& right) {
        const auto left_key = std::make_tuple(
            left.at("name").get<std::string>(),
            left.at("state").get<std::string>(),
            canonical_json(left.at("categories")));
        const auto right_key = std::make_tuple(
            right.at("name").get<std::string>(),
            right.at("state").get<std::string>(),
            canonical_json(right.at("categories")));
        return left_key < right_key;
    });

    std::set<std::string> unavailable;
    for (const auto& item : coverage.at("unavailable_categories")) unavailable.insert(item.get<std::string>());
    return Json{
        {"event_sources", sources},
        {"unavailable_categories", unavailable},
        {"dropped_events", coverage.at("dropped_events")},
        {"errors", coverage.at("errors")},
        {"recorded_event_counts", counts}
    };
}

std::vector<Json> match_events(
    const Json& baseline_manifest,
    const Json& candidate_manifest,
    const std::vector<Json>& baseline_events,
    const std::vector<Json>& candidate_events,
    const RuleProfile& profile) {
    using Groups = std::map<std::string, std::vector<Json>>;
    Groups base_groups;
    Groups candidate_groups;
    const std::set<std::string> included(profile.included_categories.begin(), profile.included_categories.end());

    for (const auto& event : baseline_events) {
        if (included.contains(event.at("category").get<std::string>())) base_groups[identity(event)].push_back(event);
    }
    for (const auto& event : candidate_events) {
        if (included.contains(event.at("category").get<std::string>())) candidate_groups[identity(event)].push_back(event);
    }

    std::set<std::string> identities;
    for (const auto& [key, _] : base_groups) identities.insert(key);
    for (const auto& [key, _] : candidate_groups) identities.insert(key);

    std::vector<Json> findings;
    for (const auto& key : identities) {
        auto before = base_groups[key];
        auto after = candidate_groups[key];
        auto order = [&](const Json& left, const Json& right) {
            return event_order(left, profile) < event_order(right, profile);
        };
        std::sort(before.begin(), before.end(), order);
        std::sort(after.begin(), after.end(), order);

        Groups base_details;
        Groups candidate_details;
        for (const auto& event : before) base_details[signature(event, profile)].push_back(event);
        for (const auto& event : after) candidate_details[signature(event, profile)].push_back(event);

        std::vector<Json> before_remaining;
        std::vector<Json> after_remaining;
        std::set<std::string> signatures;
        for (const auto& [sig, _] : base_details) signatures.insert(sig);
        for (const auto& [sig, _] : candidate_details) signatures.insert(sig);
        for (const auto& sig : signatures) {
            const auto& base_items = base_details[sig];
            const auto& candidate_items = candidate_details[sig];
            const std::size_t common = std::min(base_items.size(), candidate_items.size());
            before_remaining.insert(before_remaining.end(), base_items.begin() + static_cast<std::ptrdiff_t>(common), base_items.end());
            after_remaining.insert(after_remaining.end(), candidate_items.begin() + static_cast<std::ptrdiff_t>(common), candidate_items.end());
        }
        std::sort(before_remaining.begin(), before_remaining.end(), order);
        std::sort(after_remaining.begin(), after_remaining.end(), order);

        const std::size_t paired = std::min(before_remaining.size(), after_remaining.size());
        using ChangedKey = std::pair<std::string, std::string>;
        struct ChangedGroup { std::vector<Json> before; std::vector<Json> after; };
        std::map<ChangedKey, ChangedGroup> changed;
        for (std::size_t index = 0; index < paired; ++index) {
            const auto old_signature = signature(before_remaining[index], profile);
            const auto new_signature = signature(after_remaining[index], profile);
            auto& group = changed[{old_signature, new_signature}];
            group.before.push_back(before_remaining[index]);
            group.after.push_back(after_remaining[index]);
        }
        for (const auto& [_, group] : changed) {
            findings.push_back(make_finding(
                "changed", group.after.front().at("category").get<std::string>(),
                baseline_manifest.at("run_id").get<std::string>(),
                candidate_manifest.at("run_id").get<std::string>(),
                group.before, group.after, profile));
        }

        Groups removed;
        for (std::size_t index = paired; index < before_remaining.size(); ++index) {
            removed[signature(before_remaining[index], profile)].push_back(before_remaining[index]);
        }
        for (const auto& [_, items] : removed) {
            findings.push_back(make_finding(
                "removed", items.front().at("category").get<std::string>(),
                baseline_manifest.at("run_id").get<std::string>(),
                candidate_manifest.at("run_id").get<std::string>(),
                items, {}, profile));
        }

        Groups added;
        for (std::size_t index = paired; index < after_remaining.size(); ++index) {
            added[signature(after_remaining[index], profile)].push_back(after_remaining[index]);
        }
        for (const auto& [_, items] : added) {
            findings.push_back(make_finding(
                "added", items.front().at("category").get<std::string>(),
                baseline_manifest.at("run_id").get<std::string>(),
                candidate_manifest.at("run_id").get<std::string>(),
                {}, items, profile));
        }
    }

    std::sort(findings.begin(), findings.end(), [](const Json& left, const Json& right) {
        return std::make_tuple(
            left.at("change_type").get<std::string>(),
            left.at("category").get<std::string>(),
            left.at("finding_id").get<std::string>())
            < std::make_tuple(
                right.at("change_type").get<std::string>(),
                right.at("category").get<std::string>(),
                right.at("finding_id").get<std::string>());
    });
    return findings;
}

} // namespace

Json parse_json_strict(std::string_view source) {
    std::map<int, std::set<std::string>> object_keys;
    bool duplicate_key = false;
    auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (event == Json::parse_event_t::object_start) {
            object_keys[depth + 1].clear();
        } else if (event == Json::parse_event_t::key) {
            const auto key = parsed.get<std::string>();
            if (!object_keys[depth].insert(key).second) duplicate_key = true;
        } else if (event == Json::parse_event_t::object_end) {
            object_keys.erase(depth + 1);
        }
        return true;
    };

    Json value;
    try {
        value = Json::parse(source.begin(), source.end(), callback, true, false);
    } catch (const Json::exception&) {
        throw Error("malformed JSON");
    }
    if (duplicate_key) throw Error("duplicate JSON object keys are not valid");
    return value;
}

Json load_json_file(const std::filesystem::path& path, std::size_t maximum_bytes) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw Error("could not inspect input file '" + path.string() + "'");
    if (size > maximum_bytes) throw Error("input file exceeds its size limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("could not open input file '" + path.string() + "'");
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (content.size() > maximum_bytes) throw Error("input file exceeds its size limit");
    try {
        return parse_json_strict(content);
    } catch (const Error& error_value) {
        throw Error("input file '" + path.string() + "': " + error_value.what());
    }
}

std::vector<Json> load_jsonl(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw Error("could not inspect event file '" + path.string() + "'");
    if (size > kEventStreamLimit) throw Error("event file exceeds the 64 MiB stream limit");

    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("could not open event file '" + path.string() + "'");
    std::vector<Json> events;
    std::unordered_map<std::string, std::size_t> seen_ids;
    std::string line;
    std::size_t line_number = 0;
    std::size_t total_bytes = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::size_t line_bytes = line.size() + (input.eof() ? 0U : 1U);
        if (line_bytes > kEventLineLimit) {
            throw Error(path.string() + ":" + std::to_string(line_number) + ": record exceeds the 1 MiB line limit");
        }
        total_bytes += line_bytes;
        if (total_bytes > kEventStreamLimit) throw Error("event stream exceeds the 64 MiB size limit");
        if (line_number > kEventCountLimit) throw Error("event stream exceeds the 250000-record limit");
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            throw Error(path.string() + ":" + std::to_string(line_number) + ": blank lines are not valid JSONL records");
        }

        Json event;
        try {
            event = parse_json_strict(line);
            validate_event(event);
        } catch (const Error& error_value) {
            throw Error(path.string() + ":" + std::to_string(line_number) + ": " + error_value.what());
        }
        const auto event_id = event.at("event_id").get<std::string>();
        const auto [it, inserted] = seen_ids.emplace(event_id, line_number);
        if (!inserted) {
            throw Error(path.string() + ":" + std::to_string(line_number)
                + ": duplicate event_id (first seen on line " + std::to_string(it->second) + ")");
        }
        events.push_back(std::move(event));
    }
    if (!input.eof() && input.fail()) throw Error("could not read event file '" + path.string() + "'");
    if (events.empty()) {
        throw Error("event file '" + path.string() + "' contains no records; empty telemetry is not evidence of no activity");
    }
    return events;
}

std::string canonical_json(const Json& value) {
    return value.dump(-1, ' ', true, Json::error_handler_t::strict);
}

std::string sha256_hex(std::string_view value) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_length = 0;
    if (EVP_Digest(value.data(), value.size(), digest.data(), &digest_length, EVP_sha256(), nullptr) != 1) {
        throw Error("SHA-256 calculation failed");
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digest_length; ++index) {
        result << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    return result.str();
}

void validate_event(const Json& event) {
    allowed_keys(event, {"schema_version", "event_id", "timestamp", "category", "action", "source", "process", "resource", "outcome", "collection_quality", "attributes"}, "event");
    if (required(event, "schema_version", "event") != 1) throw Error("event.schema_version must equal 1");
    string_value(required(event, "event_id", "event"), "event.event_id", 1, 128);
    validate_timestamp(required(event, "timestamp", "event"), "event.timestamp");
    enum_value(required(event, "category", "event"), kEventCategories, "event.category");
    string_value(required(event, "action", "event"), "event.action", 1, 128);

    const auto& source = required(event, "source", "event");
    allowed_keys(source, {"collector", "event_ref"}, "event.source");
    string_value(required(source, "collector", "event.source"), "event.source.collector", 1, 128);
    string_value(required(source, "event_ref", "event.source"), "event.source.event_ref", 1, 256);

    if (has(event, "process")) validate_event_process(event["process"]);
    if (has(event, "resource")) validate_event_resource(event["resource"]);
    if (has(event, "outcome")) enum_value(event["outcome"], {"success", "failure", "unknown"}, "event.outcome");
    enum_value(required(event, "collection_quality", "event"), {"complete", "partial", "unknown"}, "event.collection_quality");
    if (has(event, "attributes")) validate_attribute_values(event["attributes"]);
}

void validate_manifest(const Json& manifest) {
    allowed_keys(manifest, {"schema_version", "run_id", "experiment_id", "role", "application", "environment", "capture", "coverage", "notes"}, "manifest");
    if (required(manifest, "schema_version", "manifest") != 1) throw Error("manifest.schema_version must equal 1");
    string_value(required(manifest, "run_id", "manifest"), "manifest.run_id", 1, 128);
    string_value(required(manifest, "experiment_id", "manifest"), "manifest.experiment_id", 1, 128);
    enum_value(required(manifest, "role", "manifest"), {"baseline", "candidate"}, "manifest.role");

    const auto& application = required(manifest, "application", "manifest");
    allowed_keys(application, {"name", "version", "artifact_sha256", "package_id"}, "manifest.application");
    string_value(required(application, "name", "manifest.application"), "manifest.application.name", 1, 256);
    string_value(required(application, "version", "manifest.application"), "manifest.application.version", 1, 128);
    const auto& artifact = required(application, "artifact_sha256", "manifest.application");
    if (!artifact.is_null()) lower_hex_digest(artifact, "manifest.application.artifact_sha256");
    if (has(application, "package_id") && !application["package_id"].is_null()) string_value(application["package_id"], "manifest.application.package_id", 0, 512);

    const auto& environment = required(manifest, "environment", "manifest");
    allowed_keys(environment, {"os", "os_version", "architecture", "snapshot_id"}, "manifest.environment");
    string_value(required(environment, "os", "manifest.environment"), "manifest.environment.os", 1, 64);
    string_value(required(environment, "os_version", "manifest.environment"), "manifest.environment.os_version", 1, 256);
    string_value(required(environment, "architecture", "manifest.environment"), "manifest.environment.architecture", 1, 64);
    if (!required(environment, "snapshot_id", "manifest.environment").is_null()) string_value(environment["snapshot_id"], "manifest.environment.snapshot_id", 0, 256);

    const auto& capture = required(manifest, "capture", "manifest");
    allowed_keys(capture, {"status", "started_at", "ended_at", "scenario", "profile", "status_reason"}, "manifest.capture");
    const auto status = string_value(required(capture, "status", "manifest.capture"), "manifest.capture.status", 1);
    if (!std::set<std::string>{"running", "completed", "incomplete", "failed"}.contains(status)) throw Error("manifest.capture.status is not in the allowed set");
    const auto& started_value = required(capture, "started_at", "manifest.capture");
    validate_timestamp(started_value, "manifest.capture.started_at");
    const auto started = *parse_timestamp(started_value.get<std::string>());
    const auto& ended_value = required(capture, "ended_at", "manifest.capture");
    std::optional<std::chrono::sys_time<std::chrono::nanoseconds>> ended;
    if (!ended_value.is_null()) {
        validate_timestamp(ended_value, "manifest.capture.ended_at");
        ended = *parse_timestamp(ended_value.get<std::string>());
        if (*ended < started) throw Error("capture.ended_at must not be earlier than capture.started_at");
    }
    string_value(required(capture, "scenario", "manifest.capture"), "manifest.capture.scenario", 1, 1024);
    string_value(required(capture, "profile", "manifest.capture"), "manifest.capture.profile", 1, 128);
    if (status == "completed" && !ended) throw Error("completed capture requires capture.ended_at");
    if (status == "running" && ended) throw Error("running capture must not have capture.ended_at");
    if ((status == "incomplete" || status == "failed")
        && (!has(capture, "status_reason") || string_value(capture["status_reason"], "manifest.capture.status_reason", 1, 2048).empty())) {
        throw Error(status + " capture requires a non-empty capture.status_reason");
    }

    const auto& coverage = required(manifest, "coverage", "manifest");
    allowed_keys(coverage, {"event_sources", "unavailable_categories", "dropped_events", "errors"}, "manifest.coverage");
    const auto& sources = required(coverage, "event_sources", "manifest.coverage");
    require_array(sources, "manifest.coverage.event_sources");
    if (sources.empty()) throw Error("manifest.coverage.event_sources must contain at least one source");
    std::set<std::string> names;
    std::set<std::string> unavailable = string_set(
        required(coverage, "unavailable_categories", "manifest.coverage"),
        "manifest.coverage.unavailable_categories", kCoverageCategories);
    for (const auto& source : sources) {
        allowed_keys(source, {"name", "state", "categories"}, "manifest.coverage.event_sources item");
        const auto name = string_value(required(source, "name", "coverage source"), "coverage source.name", 1, 128);
        const auto state = string_value(required(source, "state", "coverage source"), "coverage source.state", 1);
        if (!std::set<std::string>{"active", "inactive", "failed", "unavailable", "not_configured"}.contains(state)) {
            throw Error("coverage source.state is not in the allowed set");
        }
        if (!names.insert(name).second) throw Error("duplicate coverage.event_sources name '" + name + "'");
        const auto categories = string_set(required(source, "categories", "coverage source"), "coverage source.categories", kCoverageCategories);
        if (state != "active") {
            for (const auto& category : categories) {
                if (!unavailable.contains(category)) {
                    throw Error("coverage.unavailable_categories must include '" + category + "' for non-active source '" + name + "'");
                }
            }
        }
    }
    nonnegative_integer(required(coverage, "dropped_events", "manifest.coverage"), "manifest.coverage.dropped_events");
    const auto& errors = required(coverage, "errors", "manifest.coverage");
    require_array(errors, "manifest.coverage.errors");
    for (const auto& item : errors) string_value(item, "manifest.coverage.errors item", 1, 2048);
    if (has(manifest, "notes")) string_value(manifest["notes"], "manifest.notes", 0, 4096);
}

Json RuleProfile::report_metadata() const {
    return Json{
        {"profile_id", profile_id},
        {"sha256", sha256},
        {"included_categories", included_categories},
        {"ignored_attributes", ignored_attributes}
    };
}

RuleProfile default_rule_profile() {
    Json normalized{
        {"schema_version", 1},
        {"profile_id", "default-v1"},
        {"included_categories", {"file", "network", "process", "system"}},
        {"ignored_attributes", Json::array()}
    };
    return RuleProfile{
        "default-v1",
        {"file", "network", "process", "system"},
        {},
        sha256_hex(canonical_json(normalized))
    };
}

RuleProfile load_rule_profile(const std::filesystem::path& path) {
    const Json value = load_json_file(path, 1024U * 1024U);
    allowed_keys(value, {"schema_version", "profile_id", "included_categories", "ignored_attributes"}, "rule profile");
    if (required(value, "schema_version", "rule profile") != 1) throw Error("rule profile schema_version must equal 1");
    const auto profile_id = string_value(required(value, "profile_id", "rule profile"), "rule profile.profile_id", 1, 64);
    if (!std::regex_match(profile_id, std::regex("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"))) throw Error("rule profile.profile_id has an invalid format");

    const std::set<std::string> rule_categories{"process", "network", "file", "system"};
    auto included = string_set(required(value, "included_categories", "rule profile"), "rule profile.included_categories", rule_categories, true);
    const auto& ignored_value = required(value, "ignored_attributes", "rule profile");
    require_array(ignored_value, "rule profile.ignored_attributes");
    if (ignored_value.size() > 64U) throw Error("rule profile.ignored_attributes exceeds its 64-item limit");
    std::set<std::string> ignored;
    static const std::regex ignored_pattern("^[A-Za-z_][A-Za-z0-9_.-]{0,127}$");
    for (const auto& item : ignored_value) {
        const auto key = string_value(item, "rule profile ignored attribute", 1, 128);
        if (!std::regex_match(key, ignored_pattern)) throw Error("rule profile ignored attribute has an invalid format");
        if (!ignored.insert(key).second) throw Error("rule profile.ignored_attributes must not contain duplicates");
    }

    Json normalized{
        {"schema_version", 1},
        {"profile_id", profile_id},
        {"included_categories", std::vector<std::string>(included.begin(), included.end())},
        {"ignored_attributes", std::vector<std::string>(ignored.begin(), ignored.end())}
    };
    return RuleProfile{
        profile_id,
        std::vector<std::string>(included.begin(), included.end()),
        std::vector<std::string>(ignored.begin(), ignored.end()),
        sha256_hex(canonical_json(normalized))
    };
}

Json compare_loaded_runs(
    const Json& baseline_manifest,
    const Json& candidate_manifest,
    const std::vector<Json>& baseline_events,
    const std::vector<Json>& candidate_events,
    const RuleProfile& profile) {
    validate_manifest(baseline_manifest);
    validate_manifest(candidate_manifest);
    validate_pair(baseline_manifest, candidate_manifest);

    const auto differences = context_differences(baseline_manifest, candidate_manifest);
    auto uncertainties = coverage_uncertainties(baseline_manifest, baseline_events);
    auto candidate_uncertainties = coverage_uncertainties(candidate_manifest, candidate_events);
    uncertainties.insert(uncertainties.end(), candidate_uncertainties.begin(), candidate_uncertainties.end());

    for (const auto& difference : differences) {
        uncertainties.push_back(uncertainty(
            "run_context_differs",
            "Run context differs at " + difference.at("field").get<std::string>() + "; review this before attributing a behaviour change."));
    }

    const auto& base_hash = baseline_manifest.at("application").at("artifact_sha256");
    const auto& candidate_hash = candidate_manifest.at("application").at("artifact_sha256");
    if (!base_hash.is_null() && !candidate_hash.is_null() && base_hash == candidate_hash) {
        uncertainties.push_back(uncertainty(
            "same_artifact_hash",
            "Both runs identify the same artifact hash; this may be a repeat-run comparison rather than an update comparison."));
    }

    std::sort(uncertainties.begin(), uncertainties.end(), [](const Json& left, const Json& right) {
        const std::string left_run = left.at("run_id").is_null() ? "" : left.at("run_id").get<std::string>();
        const std::string right_run = right.at("run_id").is_null() ? "" : right.at("run_id").get<std::string>();
        return std::make_tuple(left.at("code").get<std::string>(), left_run, left.at("message").get<std::string>())
            < std::make_tuple(right.at("code").get<std::string>(), right_run, right.at("message").get<std::string>());
    });

    auto findings = match_events(baseline_manifest, candidate_manifest, baseline_events, candidate_events, profile);
    const std::set<std::string> included(profile.included_categories.begin(), profile.included_categories.end());
    auto behaviour_count = [&](const std::vector<Json>& events) {
        return std::count_if(events.begin(), events.end(), [&](const Json& event) {
            return included.contains(event.at("category").get<std::string>());
        });
    };
    const auto added = std::count_if(findings.begin(), findings.end(), [](const Json& finding) { return finding.at("change_type") == "added"; });
    const auto removed = std::count_if(findings.begin(), findings.end(), [](const Json& finding) { return finding.at("change_type") == "removed"; });
    const auto changed = std::count_if(findings.begin(), findings.end(), [](const Json& finding) { return finding.at("change_type") == "changed"; });

    Json core{
        {"schema_version", 1},
        {"status", uncertainties.empty() ? "complete" : "partial"},
        {"experiment_id", baseline_manifest.at("experiment_id")},
        {"application_name", baseline_manifest.at("application").at("name")},
        {"baseline", {
            {"run_id", baseline_manifest.at("run_id")},
            {"version", baseline_manifest.at("application").at("version")},
            {"artifact_sha256", base_hash},
            {"capture_status", baseline_manifest.at("capture").at("status")}
        }},
        {"candidate", {
            {"run_id", candidate_manifest.at("run_id")},
            {"version", candidate_manifest.at("application").at("version")},
            {"artifact_sha256", candidate_hash},
            {"capture_status", candidate_manifest.at("capture").at("status")}
        }},
        {"environment_differences", differences},
        {"comparison_profile", profile.report_metadata()},
        {"coverage", {
            {"baseline", coverage_summary(baseline_manifest, baseline_events)},
            {"candidate", coverage_summary(candidate_manifest, candidate_events)}
        }},
        {"findings", findings},
        {"uncertainties", uncertainties},
        {"summary", {
            {"baseline_event_count", baseline_events.size()},
            {"candidate_event_count", candidate_events.size()},
            {"baseline_behaviour_event_count", behaviour_count(baseline_events)},
            {"candidate_behaviour_event_count", behaviour_count(candidate_events)},
            {"added_count", added},
            {"removed_count", removed},
            {"changed_count", changed},
            {"uncertainty_count", uncertainties.size()}
        }}
    };
    return Json{{"report_id", sha256_hex(canonical_json(core))}, core};
}

Json compare_runs(
    const std::filesystem::path& baseline_manifest_path,
    const std::filesystem::path& candidate_manifest_path,
    const std::filesystem::path& baseline_events_path,
    const std::filesystem::path& candidate_events_path,
    const std::filesystem::path& profile_path) {
    Json baseline = load_json_file(baseline_manifest_path, kManifestLimit);
    Json candidate = load_json_file(candidate_manifest_path, kManifestLimit);
    validate_manifest(baseline);
    validate_manifest(candidate);
    auto baseline_events = load_jsonl(baseline_events_path);
    auto candidate_events = load_jsonl(candidate_events_path);
    const auto profile = profile_path.empty() ? default_rule_profile() : load_rule_profile(profile_path);
    return compare_loaded_runs(baseline, candidate, baseline_events, candidate_events, profile);
}

} // namespace driftglass
