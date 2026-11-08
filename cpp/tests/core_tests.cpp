#include "driftglass/core.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
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


void test_json_schema_validation_and_custom_references() {
    const auto schema = Json{
        {"type", "object"},
        {"additionalProperties", false},
        {"required", {"label"}},
        {"properties", {
            {"label", {{"$ref", "#/$defs/shortText"}}}
        }},
        {"$defs", {
            {"shortText", {{"type", "string"}, {"minLength", 3}, {"maxLength", 12}}}
        }}
    };
    driftglass::validate_json_schema(Json{{"label", "Driftglass"}}, schema);
    expect_error([&] {
        driftglass::validate_json_schema(Json{{"label", "ok"}}, schema);
    }, "custom schema references and string bounds must be enforced");
    expect_error([&] {
        driftglass::validate_json_schema(Json{{"label", "Driftglass"}, {"extra", true}}, schema);
    }, "custom schema additionalProperties=false must be enforced");

#ifdef DRIFTGLASS_SCHEMA_SOURCE_DIR
    const auto event_schema = fs::path(DRIFTGLASS_SCHEMA_SOURCE_DIR)
        / "events" / "event-v1.schema.json";
    auto valid_event = event("schema-event", "198.51.100.12", 53001, 12346);
    driftglass::validate_json_schema_file(valid_event, event_schema);
    valid_event["category"] = "telemetry_unknown";
    expect_error([&] {
        driftglass::validate_json_schema_file(valid_event, event_schema);
    }, "checked-in event schema must reject categories outside its contract");
#endif
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

void test_digest_and_status_fields_require_strings() {
    auto finding_report = comparison_report(10);
    finding_report["findings"][0]["finding_id"] = 7;
    expect_error([&] { driftglass::validate_report(finding_report); },
                 "finding IDs must reject non-string JSON values");

    auto profile_report = comparison_report(11);
    profile_report["comparison_profile"]["sha256"] = 7;
    expect_error([&] { driftglass::validate_report(profile_report); },
                 "comparison profile digests must reject non-string JSON values");

    auto report_id = comparison_report(12);
    report_id["report_id"] = 7;
    expect_error([&] { driftglass::validate_report(report_id); },
                 "report IDs must reject non-string JSON values");

    auto status_report = comparison_report(13);
    status_report["status"] = 7;
    expect_error([&] { driftglass::validate_report(status_report); },
                 "report status must reject non-string JSON values");

    auto analysis = driftglass::analyze_repeat_reports({comparison_report(14), comparison_report(15)});
    analysis["analysis_id"] = 7;
    expect_error([&] { driftglass::validate_repeat_analysis(analysis); },
                 "repeat-analysis IDs must reject non-string JSON values");
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
           << "printf '%s\\n' \"$0\" > \"$OUT.argv0\"\n"
           << "if [ \"\${DRIFTGLASS_TEST_OBSERVER_HANG:-}\" = \"1\" ]; then\n"
           << "  (sleep 18; printf 'descendant-survived\\n' > \"$OUT.child-finished\") &\n"
           << "  wait\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"\${DRIFTGLASS_TEST_OBSERVER_MALFORMED:-}\" = \"1\" ]; then\n"
           << "  printf '{malformed\\n' > \"$OUT\"\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"\${DRIFTGLASS_TEST_OBSERVER_DAEMON:-}\" = \"1\" ]; then\n"
           << "  (sleep 5; printf 'descendant-survived\\n' > \"$OUT.child-finished\") &\n"
           << "  exit 0\n"
           << "fi\n"
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

    const auto observer_alias = directory / "observer-alias.sh";
    fs::create_symlink(observer.filename(), observer_alias);
    options.observer_path = observer_alias;
    options.event_output_path = directory / "symlink-events.jsonl";
    options.manifest_output_path = directory / "symlink-manifest.json";
    const auto symlink_result = driftglass::capture_run(options);
    expect(symlink_result.completed,
           "capture through a symlinked observer path should complete");

    std::ifstream invoked_path_file(options.event_output_path.string() + ".argv0");
    std::string invoked_path;
    std::getline(invoked_path_file, invoked_path);
#if defined(__linux__)
    expect(invoked_path.find("/fd/") != std::string::npos,
           "Linux script observer should be invoked through its pinned file descriptor");
#else
    expect(invoked_path == fs::canonical(observer).string(),
           "macOS observer should receive the canonical executable path instead of the symlink alias");
#endif

    const auto daemon_observer = directory / "daemon-observer.sh";
    write_observer_fixture(daemon_observer, true);
    options.observer_path = daemon_observer;
    options.event_output_path = directory / "daemon-events.jsonl";
    options.manifest_output_path = directory / "daemon-manifest.json";
    expect(::setenv("DRIFTGLASS_TEST_OBSERVER_DAEMON", "1", 1) == 0,
           "descendant-cleanup test environment should be set");
    const auto daemon_result = driftglass::capture_run(options);
    (void)::unsetenv("DRIFTGLASS_TEST_OBSERVER_DAEMON");
    expect(daemon_result.completed,
           "a valid observer should complete even when it leaves a child process");
    std::this_thread::sleep_for(std::chrono::seconds(6));
    expect(!fs::exists(options.event_output_path.string() + ".child-finished"),
           "observer exit must terminate remaining descendants in its process group");

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

    const auto malformed_observer = directory / "malformed-observer.sh";
    write_observer_fixture(malformed_observer, true);
    options.observer_path = malformed_observer;
    options.event_output_path = directory / "malformed-events.jsonl";
    options.manifest_output_path = directory / "malformed-manifest.json";
    expect(::setenv("DRIFTGLASS_TEST_OBSERVER_MALFORMED", "1", 1) == 0,
           "malformed-output test environment should be set");
    const auto malformed = driftglass::capture_run(options);
    (void)::unsetenv("DRIFTGLASS_TEST_OBSERVER_MALFORMED");
    expect(!malformed.completed && malformed.manifest.at("capture").at("status") == "failed",
           "malformed observer JSONL must produce a failed capture manifest");
    expect(malformed.manifest.at("coverage").at("errors").dump().find("JSONL validation") != std::string::npos,
           "malformed observer output must be visible in manifest errors");

    const auto invalid_observer = directory / "invalid-observer";
    {
        std::ofstream invalid(invalid_observer, std::ios::binary);
        invalid << "not an executable format\n";
    }
    fs::permissions(invalid_observer,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);
    options.observer_path = invalid_observer;
    options.event_output_path = directory / "invalid-events.jsonl";
    options.manifest_output_path = directory / "invalid-manifest.json";
    const auto invalid = driftglass::capture_run(options);
    expect(!invalid.completed
           && invalid.manifest.at("coverage").at("errors").dump().find("observer could not be started") != std::string::npos,
           "an executable with an unsupported format must fail visibly in the manifest");

    const auto hanging_observer = directory / "hanging-observer.sh";
    write_observer_fixture(hanging_observer, true);
    options.observer_path = hanging_observer;
    options.event_output_path = directory / "hanging-events.jsonl";
    options.manifest_output_path = directory / "hanging-manifest.json";
    expect(::setenv("DRIFTGLASS_TEST_OBSERVER_HANG", "1", 1) == 0,
           "timeout test environment should be set");
    const auto timed_out = driftglass::capture_run(options);
    (void)::unsetenv("DRIFTGLASS_TEST_OBSERVER_HANG");
    expect(!timed_out.completed
           && timed_out.manifest.at("coverage").at("errors").dump().find("time limit") != std::string::npos,
           "observer timeout must be reported as an incomplete capture");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    expect(!fs::exists(options.event_output_path.string() + ".child-finished"),
           "observer timeout must terminate descendants in the observer process group");

    fs::remove_all(directory);
}
#endif

void test_capabilities_contract_describes_mac_process_only_scope() {
    const auto report = driftglass::capabilities_report();
    expect(report.at("schema_version") == 1, "capability report schema must be v1");
    const auto& collector = report.at("collector");
    expect(collector.at("platform") == "Linux", "integrated procfs collector remains Linux-specific");
    expect(collector.at("implemented_categories") == Json::array({"process", "network"}),
           "Linux collector exposes only process and socket metadata categories");
    const auto& platforms = report.at("platforms");
    expect(platforms.size() == 4, "capabilities must declare all four platform classes");
    bool mac_found = false;
    for (const auto& platform : platforms) {
        if (platform.at("platform") == "macOS") {
            mac_found = true;
            expect(platform.at("collector_status") == "experimental",
                   "macOS process-only helper must be reported as experimental");
            const auto status = platform.at("validation_status").get<std::string>();
            expect(status.find("process-only") != std::string::npos
                   && status.find("Endpoint Security") != std::string::npos,
                   "macOS capability status must explain missing Endpoint Security");
        }
    }
    expect(mac_found, "capability report must include macOS");
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
        run_test("json_schema_validation", test_json_schema_validation_and_custom_references);
        run_test("capture_manifest_invariants", test_capture_manifest_invariants);
        run_test("local_socket_noise", test_local_socket_noise_is_ignored_but_evidence_is_preserved);
        run_test("remote_endpoint_change", test_remote_endpoint_change_keeps_raw_evidence);
        run_test("deterministic_report_identity", test_deterministic_report_identity);
        run_test("profile_ignored_attributes", test_profile_ignored_attributes_remain_in_evidence);
        run_test("digest_and_status_string_types", test_digest_and_status_fields_require_strings);
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
