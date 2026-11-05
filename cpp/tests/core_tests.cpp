#include "driftglass/core.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using driftglass::Json;

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expect_error(const std::function<void()>& function, const std::string& message) {
    try {
        function();
    } catch (const driftglass::Error&) {
        return;
    }
    throw std::runtime_error(message);
}

Json manifest(std::string run_id, std::string role, std::string version, std::string artifact_hash) {
    return Json{
        {"schema_version", 1},
        {"run_id", std::move(run_id)},
        {"experiment_id", "experiment-1"},
        {"role", std::move(role)},
        {"application", {
            {"name", "Fixture App"},
            {"version", std::move(version)},
            {"artifact_sha256", std::move(artifact_hash)},
            {"package_id", nullptr}
        }},
        {"environment", {
            {"os", "Linux"},
            {"os_version", "fixture"},
            {"architecture", "x86_64"},
            {"snapshot_id", "snapshot-1"}
        }},
        {"capture", {
            {"status", "completed"},
            {"started_at", "2026-01-01T00:00:00Z"},
            {"ended_at", "2026-01-01T00:00:05Z"},
            {"scenario", "idle"},
            {"profile", "default"}
        }},
        {"coverage", {
            {"event_sources", Json::array({
                Json{{"name", "fixture-process"}, {"state", "active"}, {"categories", {"process"}}},
                Json{{"name", "fixture-network"}, {"state", "active"}, {"categories", {"network"}}}
            })},
            {"unavailable_categories", {"file", "system", "persistence"}},
            {"dropped_events", 0},
            {"errors", Json::array()}
        }}
    };
}

Json event(std::string id, std::string remote, int local_port, std::uint64_t inode) {
    return Json{
        {"schema_version", 1},
        {"event_id", std::move(id)},
        {"timestamp", "2026-01-01T00:00:01Z"},
        {"category", "network"},
        {"action", "socket_observed"},
        {"source", {{"collector", "fixture"}, {"event_ref", "fixture:" + std::to_string(inode)}}},
        {"process", {{"pid", 10}, {"parent_pid", 1}, {"executable", "/opt/fixture"}}},
        {"resource", {
            {"kind", "network"},
            {"identifier", "127.0.0.1:" + std::to_string(local_port)},
            {"address", std::move(remote)},
            {"port", 443},
            {"protocol", "tcp"}
        }},
        {"collection_quality", "complete"},
        {"attributes", {{"local_port", local_port}, {"socket_inode", inode}}}
    };
}

void test_strict_json() {
    expect_error([] { (void)driftglass::parse_json_strict(R"({"event_id":"a","event_id":"b"})"); },
                 "duplicate JSON object keys must be rejected");
    const auto value = driftglass::parse_json_strict(R"({"a":{"b":1},"c":[true,null]})");
    expect(value.at("a").at("b") == 1, "valid nested JSON should parse");
}

void test_event_validation() {
    Json valid = event("event-1", "198.51.100.10", 53000, 12345);
    driftglass::validate_event(valid);
    valid["attributes"]["bad key"] = "no";
    expect_error([&] { driftglass::validate_event(valid); }, "invalid event attribute names must fail");
}

void test_capture_manifest_invariants() {
    auto valid = manifest("base", "baseline", "1.0", std::string(64, 'a'));
    driftglass::validate_manifest(valid);
    valid["capture"]["ended_at"] = nullptr;
    expect_error([&] { driftglass::validate_manifest(valid); }, "completed capture without end time must fail");
}

void test_local_socket_noise_is_ignored_but_evidence_is_preserved() {
    const auto base_manifest = manifest("base", "baseline", "1.0", std::string(64, 'a'));
    const auto candidate_manifest = manifest("candidate", "candidate", "2.0", std::string(64, 'b'));
    const auto report = driftglass::compare_loaded_runs(
        base_manifest, candidate_manifest,
        {event("base-event", "198.51.100.10", 53000, 12345)},
        {event("candidate-event", "198.51.100.10", 54000, 67890)});

    expect(report.is_object(), "comparison output must be a report object");
    expect(report.contains("report_id"), "comparison output must include report_id");
    expect(report.at("findings").empty(), "changing only local port and socket inode must not add a finding");
    expect(report.at("summary").at("changed_count") == 0, "volatile local socket metadata must not count as a change");
}

void test_remote_endpoint_change_keeps_raw_evidence() {
    const auto base_manifest = manifest("base", "baseline", "1.0", std::string(64, 'a'));
    const auto candidate_manifest = manifest("candidate", "candidate", "2.0", std::string(64, 'b'));
    const auto report = driftglass::compare_loaded_runs(
        base_manifest, candidate_manifest,
        {event("base-event", "198.51.100.10", 53000, 12345)},
        {event("candidate-event", "198.51.100.11", 54000, 67890)});

    expect(report.at("summary").at("changed_count") == 1, "a changed remote endpoint must remain a finding");
    const auto& finding = report.at("findings").at(0);
    expect(finding.at("change_type") == "changed", "remote endpoint difference must be classified as changed");
    expect(finding.at("baseline_evidence").at(0).at("observed").at("resource").at("identifier") == "127.0.0.1:53000",
           "baseline raw local identifier must remain in evidence");
    expect(finding.at("candidate_evidence").at(0).at("observed").at("attributes").at("socket_inode") == 67890,
           "candidate raw socket inode must remain in evidence");
}

void test_deterministic_report_identity() {
    const auto base_manifest = manifest("base", "baseline", "1.0", std::string(64, 'a'));
    const auto candidate_manifest = manifest("candidate", "candidate", "2.0", std::string(64, 'b'));
    const std::vector<Json> before{event("base-event", "198.51.100.10", 53000, 12345)};
    const std::vector<Json> after{event("candidate-event", "198.51.100.11", 54000, 67890)};
    const auto first = driftglass::compare_loaded_runs(base_manifest, candidate_manifest, before, after);
    const auto second = driftglass::compare_loaded_runs(base_manifest, candidate_manifest, before, after);
    expect(first.at("report_id") == second.at("report_id"), "identical inputs must produce the same report ID");
}


void test_profile_ignored_attributes_remain_in_evidence() {
    auto base_manifest = manifest("base", "baseline", "1.0", std::string(64, 'a'));
    auto candidate_manifest = manifest("candidate", "candidate", "2.0", std::string(64, 'b'));
    base_manifest["application"].erase("package_id");
    candidate_manifest["application"].erase("package_id");

    auto before = event("base-event", "198.51.100.10", 53000, 12345);
    auto after = event("candidate-event", "198.51.100.11", 54000, 67890);
    before["attributes"]["vendor_tag"] = "baseline-only";
    after["attributes"]["vendor_tag"] = "candidate-only";

    auto profile = driftglass::default_rule_profile();
    profile.ignored_attributes = {"vendor_tag"};

    const auto report = driftglass::compare_loaded_runs(
        base_manifest, candidate_manifest, {before}, {after}, profile);

    expect(report.at("summary").at("changed_count") == 1,
           "remote behavior difference should remain visible when an attribute is ignored");
    const auto& finding = report.at("findings").at(0);
    expect(finding.at("baseline_evidence").at(0).at("observed").at("attributes").at("vendor_tag") == "baseline-only",
           "baseline evidence must preserve attributes ignored for matching");
    expect(finding.at("candidate_evidence").at(0).at("observed").at("attributes").at("vendor_tag") == "candidate-only",
           "candidate evidence must preserve attributes ignored for matching");
}


Json comparison_report(int index) {
    const auto suffix = std::to_string(index);
    const auto baseline = manifest("base-" + suffix, "baseline", "1.0", std::string(64, 'a'));
    const auto candidate = manifest("candidate-" + suffix, "candidate", "2.0", std::string(64, 'b'));
    const auto before = event("before-" + suffix, "198.51.100.10", 53000, 12345);
    const auto after = event("after-" + suffix, "198.51.100.11", 53000, 12345);
    return driftglass::compare_loaded_runs(baseline, candidate, {before}, {after});
}

void test_report_validation_repeat_analysis_and_integrity() {
    const auto first = comparison_report(1);
    const auto second = comparison_report(2);
    driftglass::validate_report(first);
    driftglass::validate_report(second);

    const auto analysis = driftglass::analyze_repeat_reports({first, second});
    driftglass::validate_repeat_analysis(analysis);
    expect(analysis.at("report_count") == 2, "repeat analysis must include both source reports");
    expect(analysis.at("summary").at("recurring_pattern_count") == 1,
           "same observed difference should recur across the two reports");

    const auto folder = fs::temp_directory_path()
        / ("driftglass-native-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(folder);
    const auto report_path = folder / "report.json";
    const auto manifest_path = folder / "report.integrity.json";
    {
        std::ofstream output(report_path, std::ios::binary);
        output << first.dump(2, ' ', true) << "\n";
    }
    const auto integrity = driftglass::attest_report(report_path);
    {
        std::ofstream output(manifest_path, std::ios::binary);
        output << integrity.dump(2, ' ', true) << "\n";
    }
    const auto verified = driftglass::verify_report_integrity(report_path, manifest_path);
    expect(verified.at("report_id") == first.at("report_id"),
           "integrity verification must return the validated report identity");

    {
        std::ofstream output(report_path, std::ios::binary | std::ios::app);
        output << " ";
    }
    expect_error([&] { (void)driftglass::verify_report_integrity(report_path, manifest_path); },
                 "integrity verification must detect byte-level report edits");
    fs::remove_all(folder);
}


void test_report_redaction_is_deterministic_and_preserves_source() {
    const auto source = comparison_report(3);
    const auto redacted = driftglass::redact_report(source, true, true, true);
    driftglass::validate_report(redacted);
    expect(source.at("report_id") != redacted.at("report_id"),
           "redacted report must receive a new report identity");
    expect(source.at("findings").at(0).at("finding_id") != redacted.at("findings").at(0).at("finding_id"),
           "redacted finding must receive a new finding identity");
    expect(canonical_json(source).find("/opt/fixture") != std::string::npos,
           "redaction must not mutate the caller's source report");
    const auto serialized = canonical_json(redacted);
    expect(serialized.find("/opt/fixture") == std::string::npos,
           "redacted report must not expose original process paths");
    expect(serialized.find("198.51.100.10") == std::string::npos
           && serialized.find("198.51.100.11") == std::string::npos,
           "redacted report must not expose original network addresses");
    expect(serialized.find("[REDACTED_PATH_") != std::string::npos,
           "redacted paths should be replaced with stable tokens");
    expect_error([&] { (void)driftglass::redact_report(source, false, false, false); },
                 "redaction without any category must fail");
}

} // namespace

template <typename Function>
void run_test(const char* name, Function&& function) {
    try {
        function();
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(name) + ": " + error.what());
    }
}

int main() {
    try {
        run_test("strict_json", test_strict_json);
        run_test("event_validation", test_event_validation);
        run_test("capture_manifest_invariants", test_capture_manifest_invariants);
        run_test("local_socket_noise", test_local_socket_noise_is_ignored_but_evidence_is_preserved);
        run_test("remote_endpoint_change", test_remote_endpoint_change_keeps_raw_evidence);
        run_test("deterministic_report_identity", test_deterministic_report_identity);
        run_test("profile_ignored_attributes", test_profile_ignored_attributes_remain_in_evidence);
        run_test("report_repeat_integrity", test_report_validation_repeat_analysis_and_integrity);
        run_test("report_redaction", test_report_redaction_is_deterministic_and_preserves_source);
        std::cout << "C++ core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "C++ core test failure: " << error.what() << "\n";
        return 1;
    }
}
