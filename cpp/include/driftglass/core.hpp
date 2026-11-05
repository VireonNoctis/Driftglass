#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace driftglass {

using Json = nlohmann::json;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RuleProfile {
    std::string profile_id;
    std::vector<std::string> included_categories;
    std::vector<std::string> ignored_attributes;
    std::string sha256;

    [[nodiscard]] Json report_metadata() const;
};

[[nodiscard]] Json parse_json_strict(std::string_view source);
[[nodiscard]] Json load_json_file(const std::filesystem::path& path, std::size_t maximum_bytes);
[[nodiscard]] std::vector<Json> load_jsonl(const std::filesystem::path& path);
[[nodiscard]] std::string canonical_json(const Json& value);
[[nodiscard]] std::string sha256_hex(std::string_view value);

void validate_event(const Json& event);
void validate_manifest(const Json& manifest);

[[nodiscard]] RuleProfile default_rule_profile();
[[nodiscard]] RuleProfile load_rule_profile(const std::filesystem::path& path);

[[nodiscard]] Json compare_loaded_runs(
    const Json& baseline_manifest,
    const Json& candidate_manifest,
    const std::vector<Json>& baseline_events,
    const std::vector<Json>& candidate_events,
    const RuleProfile& profile = default_rule_profile());

void validate_report(const Json& report);
[[nodiscard]] std::string render_html_report(const Json& report);
[[nodiscard]] Json redact_report(const Json& report, bool redact_paths, bool redact_network, bool redact_process_names);
[[nodiscard]] Json analyze_repeat_reports(const std::vector<Json>& reports);
void validate_repeat_analysis(const Json& analysis);
[[nodiscard]] std::string save_report(const std::filesystem::path& database_path, const Json& report);
[[nodiscard]] Json list_reports(const std::filesystem::path& database_path, std::size_t limit = 100);
[[nodiscard]] std::optional<Json> get_report(const std::filesystem::path& database_path, const std::string& report_id);
[[nodiscard]] bool delete_report(const std::filesystem::path& database_path, const std::string& report_id);
[[nodiscard]] Json attest_report(const std::filesystem::path& report_path);
[[nodiscard]] Json verify_report_integrity(const std::filesystem::path& report_path, const std::filesystem::path& manifest_path);

[[nodiscard]] Json compare_runs(
    const std::filesystem::path& baseline_manifest,
    const std::filesystem::path& candidate_manifest,
    const std::filesystem::path& baseline_events,
    const std::filesystem::path& candidate_events,
    const std::filesystem::path& profile = {});

} // namespace driftglass
