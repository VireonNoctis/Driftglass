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

} // namespace

int main() {
    try {
        test_strict_json();
        test_event_validation();
        test_capture_manifest_invariants();
        test_local_socket_noise_is_ignored_but_evidence_is_preserved();
        test_remote_endpoint_change_keeps_raw_evidence();
        test_deterministic_report_identity();
        std::cout << "C++ core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "C++ core test failure: " << error.what() << "\n";
        return 1;
    }
}
