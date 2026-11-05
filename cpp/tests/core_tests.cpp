#include "driftglass/core.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

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
    expect(driftglass::canonical_json(source).find("/opt/fixture") != std::string::npos,
           "redaction must not mutate the caller's source report");
    const auto serialized = driftglass::canonical_json(redacted);
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


void test_sqlite_history_is_transactional_idempotent_and_explicit() {
    const auto directory = fs::temp_directory_path()
        / ("driftglass-history-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(directory);
    const auto database = directory / "history.sqlite";
    const auto report = comparison_report(8);
    const auto id = report.at("report_id").get<std::string>();

    expect(driftglass::save_report(database, report) == id, "history save must return the report ID");
    expect(driftglass::save_report(database, report) == id, "saving identical content must be idempotent");
    const auto rows = driftglass::list_reports(database);
    expect(rows.size() == 1, "idempotent save must not duplicate history rows");
    expect(rows.at(0).at("report_id") == id, "history list must return the stored report");
    const auto restored = driftglass::get_report(database, id);
    expect(restored.has_value() && restored->at("report_id") == id, "history get must validate and return its report");

    expect_error([&] { (void)driftglass::list_reports(database, 0); },
                 "history limit below one must fail");
    expect_error([&] { (void)driftglass::get_report(database, "not-a-report-id"); },
                 "invalid history report IDs must fail");

    expect(driftglass::delete_report(database, id), "explicit API delete must remove an existing report");
    expect(!driftglass::get_report(database, id).has_value(), "deleted report must not remain readable");
    expect(!driftglass::delete_report(database, id), "repeated delete must report absence");
    fs::remove_all(directory);
}


void test_html_report_is_standalone_and_escapes_untrusted_content() {
    auto base = manifest("base-html", "baseline", "1.0", std::string(64, 'a'));
    auto candidate = manifest("candidate-html", "candidate", "2.0", std::string(64, 'b'));
    base["application"]["name"] = "<script>alert(1)</script>";
    candidate["application"]["name"] = "<script>alert(1)</script>";
    const auto report = driftglass::compare_loaded_runs(
        base, candidate,
        {event("html-before", "198.51.100.10", 52000, 100)},
        {event("html-after", "198.51.100.11", 52000, 100)});
    const auto html = driftglass::render_html_report(report);
    expect(html.find("<!doctype html>") != std::string::npos, "HTML export must be a complete document");
    expect(html.find("&lt;script&gt;alert(1)&lt;/script&gt;") != std::string::npos,
           "HTML export must escape untrusted application names");
    expect(html.find("<script>alert(1)</script>") == std::string::npos,
           "HTML export must not turn report text into executable markup");
    expect(html.find("https://") == std::string::npos,
           "HTML export must not load remote assets");
}


#if defined(__APPLE__) || defined(__linux__)
void write_observer_fixture(const fs::path& path, bool successful_exit) {
    std::ofstream script(path, std::ios::binary);
    script << "#!/bin/sh\n"
           << "PID=''\nOUT=''\n"
           << "while [ \"$#\" -gt 0 ]; do\n"
           << "  case \"$1\" in\n"
           << "    --pid) PID=\"$2\"; shift 2;;\n"
           << "    --output) OUT=\"$2\"; shift 2;;\n"
           << "    *) shift;;\n"
           << "  esac\n"
           << "done\n"
           << "printf '{\"schema_version\":1,\"event_id\":\"start\",\"timestamp\":\"2026-01-01T00:00:00Z\",\"category\":\"coverage\",\"action\":\"capture_started\",\"source\":{\"collector\":\"fixture\",\"event_ref\":\"capture:start\"},\"collection_quality\":\"complete\",\"attributes\":{\"target_pid\":%s}}\\n' \"$PID\" > \"$OUT\"\n"
           << "printf '{\"schema_version\":1,\"event_id\":\"finish\",\"timestamp\":\"2026-01-01T00:00:01Z\",\"category\":\"coverage\",\"action\":\"capture_finished\",\"source\":{\"collector\":\"fixture\",\"event_ref\":\"capture:finish\"},\"collection_quality\":\"complete\",\"attributes\":{\"capture_status\":\"completed\"}}\\n' >> \"$OUT\"\n"
           << (successful_exit ? "exit 0\n" : "exit 4\n");
    script.close();
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);
}

void test_capture_coordinator_validates_boundaries_and_publishes_manifest() {
    const auto directory = fs::temp_directory_path()
        / ("driftglass-capture-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(directory);
    const auto observer = directory / "fixture-observer.sh";
    write_observer_fixture(observer, true);

    driftglass::CaptureOptions options;
    options.observer_path = observer;
    options.pid = static_cast<std::uint32_t>(::getpid());
    options.duration_seconds = 1;
    options.interval_ms = 100;
    options.experiment_id = "capture-test";
    options.role = "baseline";
    options.application_name = "Fixture App";
    options.application_version = "1.0";
    options.event_output_path = directory / "events.jsonl";
    options.manifest_output_path = directory / "manifest.json";
    options.scenario = "synthetic capture contract";
    options.profile = "fixture-process-observer";

    const auto result = driftglass::capture_run(options);
    expect(result.completed, "valid observer run should complete");
    expect(result.event_count == 2, "capture coordinator should count validated event records");
    expect(result.manifest.at("capture").at("status") == "completed",
           "completed observer should produce a completed manifest");
    driftglass::validate_manifest(result.manifest);
    expect(fs::is_regular_file(result.event_path) && fs::is_regular_file(result.manifest_path),
           "capture should publish event and manifest output files");

    expect_error([&] { (void)driftglass::capture_run(options); },
                 "capture outputs must not be overwritten");

    const auto failing_observer = directory / "failing-observer.sh";
    write_observer_fixture(failing_observer, false);
    options.observer_path = failing_observer;
    options.event_output_path = directory / "failed-events.jsonl";
    options.manifest_output_path = directory / "failed-manifest.json";
    const auto failed = driftglass::capture_run(options);
    expect(!failed.completed, "non-zero observer exit must not be reported as completed");
    expect(failed.manifest.at("capture").at("status") == "incomplete",
           "a valid stream with a failed observer exit should produce an incomplete manifest");
    expect(!failed.manifest.at("coverage").at("errors").empty(),
           "observer exit failure should be visible in manifest coverage errors");
    fs::remove_all(directory);
}
#endif

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
        run_test("sqlite_history", test_sqlite_history_is_transactional_idempotent_and_explicit);
        run_test("html_report", test_html_report_is_standalone_and_escapes_untrusted_content);
#if defined(__APPLE__) || defined(__linux__)
        run_test("capture_coordinator", test_capture_coordinator_validates_boundaries_and_publishes_manifest);
#endif
        std::cout << "C++ core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "C++ core test failure: " << error.what() << "\n";
        return 1;
    }
}
