#include "driftglass/core.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <tuple>

namespace driftglass {
namespace {

constexpr std::size_t kReportLimit = 64U * 1024U * 1024U;
constexpr std::size_t kIntegrityLimit = 64U * 1024U;

const std::set<std::string> kCategories{"process", "network", "file", "system"};
const std::set<std::string> kCoverageCategories{"process", "network", "file", "system", "persistence"};
const std::set<std::string> kUncertaintyCodes{
    "capture_not_completed", "categories_unavailable", "event_sources_not_active",
    "events_dropped", "collection_errors", "coverage_not_declared", "artifact_hash_missing"
};

bool has(const Json& value, const char* key) {
    return value.is_object() && value.contains(key);
}

void object(const Json& value, const std::string& label) {
    if (!value.is_object()) throw Error(label + " must be an object");
}

void array(const Json& value, const std::string& label) {
    if (!value.is_array()) throw Error(label + " must be an array");
}

void keys(const Json& value, const std::set<std::string>& allowed, const std::string& label) {
    object(value, label);
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!allowed.contains(it.key())) throw Error(label + " contains unsupported field '" + it.key() + "'");
    }
}

const Json& need(const Json& value, const char* key, const std::string& label) {
    if (!has(value, key)) throw Error(label + " is missing required field '" + key + "'");
    return value.at(key);
}

std::string text(const Json& value, const std::string& label, std::size_t minimum = 0,
                 std::size_t maximum = static_cast<std::size_t>(-1)) {
    if (!value.is_string()) throw Error(label + " must be a string");
    const auto& result = value.get_ref<const std::string&>();
    if (result.size() < minimum || result.size() > maximum) throw Error(label + " has an invalid length");
    return result;
}

bool integer(const Json& value) {
    return value.is_number_integer() || value.is_number_unsigned();
}

std::uint64_t count(const Json& value, const std::string& label) {
    if (!integer(value)) throw Error(label + " must be a non-negative integer");
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) throw Error(label + " must be a non-negative integer");
    return static_cast<std::uint64_t>(signed_value);
}

void digest(const Json& value, const std::string& label, bool nullable = false) {
    if (nullable && value.is_null()) return;
    const auto candidate = text(value, label, 64, 64);
    if (!std::all_of(candidate.begin(), candidate.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) throw Error(label + " must be a lowercase SHA-256 digest");
}

void enum_text(const Json& value, const std::set<std::string>& allowed, const std::string& label) {
    const auto candidate = text(value, label, 1);
    if (!allowed.contains(candidate)) throw Error(label + " is not in the allowed set");
}

std::set<std::string> string_set(const Json& value, const std::set<std::string>& allowed,
                                 const std::string& label, bool nonempty = false) {
    array(value, label);
    if (nonempty && value.empty()) throw Error(label + " must not be empty");
    std::set<std::string> result;
    for (const auto& item : value) {
        const auto candidate = text(item, label + " item", 1, 128);
        if (!allowed.contains(candidate)) throw Error(label + " contains unsupported value '" + candidate + "'");
        if (!result.insert(candidate).second) throw Error(label + " contains duplicates");
    }
    return result;
}

void validate_run(const Json& run, const std::string& label) {
    keys(run, {"run_id", "version", "artifact_sha256", "capture_status"}, label);
    text(need(run, "run_id", label), label + ".run_id", 1, 128);
    text(need(run, "version", label), label + ".version", 1, 128);
    digest(need(run, "artifact_sha256", label), label + ".artifact_sha256", true);
    enum_text(need(run, "capture_status", label), {"completed", "incomplete", "failed"}, label + ".capture_status");
}

void validate_observed(const Json& observed, const std::string& label, const std::string& finding_category) {
    object(observed, label);
    enum_text(need(observed, "category", label), kCategories, label + ".category");
    if (observed.at("category") != finding_category) throw Error(label + ".category does not match finding category");
    text(need(observed, "action", label), label + ".action", 1, 128);
    const auto& executable = need(observed, "process_executable", label);
    if (!executable.is_null()) text(executable, label + ".process_executable", 0, 4096);
    const auto& resource = need(observed, "resource", label);
    if (!resource.is_null()) object(resource, label + ".resource");
    const auto& outcome = need(observed, "outcome", label);
    if (!outcome.is_null()) enum_text(outcome, {"success", "failure", "unknown"}, label + ".outcome");
    object(need(observed, "attributes", label), label + ".attributes");
    if (has(observed, "process_identity")) {
        const auto& identity = observed.at("process_identity");
        keys(identity, {"pid", "parent_pid", "executable", "start_time_ticks"}, label + ".process_identity");
        count(need(identity, "pid", label + ".process_identity"), label + ".process_identity.pid");
        count(need(identity, "parent_pid", label + ".process_identity"), label + ".process_identity.parent_pid");
        text(need(identity, "executable", label + ".process_identity"), label + ".process_identity.executable", 0, 4096);
        if (has(identity, "start_time_ticks")) count(identity.at("start_time_ticks"), label + ".process_identity.start_time_ticks");
    }
}

void validate_evidence(const Json& evidence, const std::string& expected_run, const std::string& category) {
    keys(evidence, {"run_id", "event_id", "collector", "source_event_ref", "timestamp", "observed"}, "finding evidence");
    if (text(need(evidence, "run_id", "evidence"), "evidence.run_id", 1, 128) != expected_run) {
        throw Error("finding evidence references an unexpected run_id");
    }
    text(need(evidence, "event_id", "evidence"), "evidence.event_id", 1, 128);
    text(need(evidence, "collector", "evidence"), "evidence.collector", 1, 128);
    text(need(evidence, "source_event_ref", "evidence"), "evidence.source_event_ref", 1, 256);
    text(need(evidence, "timestamp", "evidence"), "evidence.timestamp", 1, 64);
    validate_observed(need(evidence, "observed", "evidence"), "evidence.observed", category);
}

void validate_finding(const Json& finding, const std::string& baseline_run, const std::string& candidate_run) {
    keys(finding, {"finding_id", "rule_id", "change_type", "category", "summary", "observation",
                   "interpretation", "baseline_evidence", "candidate_evidence"}, "finding");
    digest(need(finding, "finding_id", "finding"), "finding.finding_id");
    enum_text(need(finding, "rule_id", "finding"), {"DG-COMPARE-001", "DG-COMPARE-002"}, "finding.rule_id");
    const auto change = text(need(finding, "change_type", "finding"), "finding.change_type", 1);
    enum_text(finding.at("change_type"), {"added", "removed", "changed"}, "finding.change_type");
    enum_text(need(finding, "category", "finding"), kCategories, "finding.category");
    text(need(finding, "summary", "finding"), "finding.summary", 1, 1024);
    text(need(finding, "observation", "finding"), "finding.observation", 1, 2048);
    text(need(finding, "interpretation", "finding"), "finding.interpretation", 1, 2048);

    const auto& before = need(finding, "baseline_evidence", "finding");
    const auto& after = need(finding, "candidate_evidence", "finding");
    array(before, "finding.baseline_evidence");
    array(after, "finding.candidate_evidence");
    if ((change == "added" && (!before.empty() || after.empty()))
        || (change == "removed" && (before.empty() || !after.empty()))
        || (change == "changed" && (before.empty() || after.empty()))) {
        throw Error("finding evidence arrays do not match change_type");
    }
    for (const auto& item : before) validate_evidence(item, baseline_run, finding.at("category").get<std::string>());
    for (const auto& item : after) validate_evidence(item, candidate_run, finding.at("category").get<std::string>());

    Json identity = finding;
    identity.erase("finding_id");
    identity.erase("interpretation");
    if (sha256_hex(canonical_json(identity)) != finding.at("finding_id")) {
        throw Error("finding_id does not match finding content");
    }
}

void validate_coverage(const Json& value, const std::string& role) {
    keys(value, {"event_sources", "unavailable_categories", "dropped_events", "errors", "recorded_event_counts"},
         "coverage." + role);
    const auto& sources = need(value, "event_sources", "coverage." + role);
    array(sources, "coverage." + role + ".event_sources");
    if (sources.empty()) throw Error("coverage." + role + ".event_sources must not be empty");
    std::set<std::string> names;
    for (const auto& source : sources) {
        keys(source, {"name", "state", "categories"}, "coverage source");
        const auto name = text(need(source, "name", "coverage source"), "coverage source.name", 1, 128);
        if (!names.insert(name).second) throw Error("coverage source names must be unique");
        enum_text(need(source, "state", "coverage source"),
                  {"active", "inactive", "failed", "unavailable", "not_configured"}, "coverage source.state");
        string_set(need(source, "categories", "coverage source"), kCoverageCategories, "coverage source.categories");
    }
    string_set(need(value, "unavailable_categories", "coverage." + role),
               kCoverageCategories, "coverage." + role + ".unavailable_categories");
    count(need(value, "dropped_events", "coverage." + role), "coverage." + role + ".dropped_events");
    const auto& errors = need(value, "errors", "coverage." + role);
    array(errors, "coverage." + role + ".errors");
    for (const auto& error : errors) text(error, "coverage error", 1, 2048);
    const auto& counts = need(value, "recorded_event_counts", "coverage." + role);
    keys(counts, {"process", "network", "file", "system", "coverage"}, "recorded_event_counts");
    for (const auto& key : {"process", "network", "file", "system", "coverage"}) {
        count(need(counts, key, "recorded_event_counts"), std::string("recorded_event_counts.") + key);
    }
}

Json cohort_for(const Json& report) {
    Json profile_hash = nullptr;
    if (has(report, "comparison_profile")) profile_hash = report.at("comparison_profile").at("sha256");
    return Json{
        {"application_name", report.at("application_name")},
        {"baseline_version", report.at("baseline").at("version")},
        {"candidate_version", report.at("candidate").at("version")},
        {"baseline_artifact_sha256", report.at("baseline").at("artifact_sha256")},
        {"candidate_artifact_sha256", report.at("candidate").at("artifact_sha256")},
        {"comparison_profile_sha256", profile_hash}
    };
}

std::string observed_signature(const Json& evidence, const std::set<std::string>& ignored) {
    Json observed = evidence.at("observed");
    observed.erase("process_identity");
    if (has(observed, "attributes") && observed.at("attributes").is_object()) {
        observed["attributes"].erase("process_start_time_ticks");
        for (const auto& key : ignored) observed["attributes"].erase(key);
    }
    return canonical_json(observed);
}

std::string finding_pattern(const Json& finding, const std::set<std::string>& ignored) {
    std::vector<std::string> before;
    std::vector<std::string> after;
    for (const auto& evidence : finding.at("baseline_evidence")) before.push_back(observed_signature(evidence, ignored));
    for (const auto& evidence : finding.at("candidate_evidence")) after.push_back(observed_signature(evidence, ignored));
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    return sha256_hex(canonical_json(Json{
        {"rule_id", finding.at("rule_id")},
        {"change_type", finding.at("change_type")},
        {"category", finding.at("category")},
        {"baseline_observations", before},
        {"candidate_observations", after}
    }));
}

std::string read_bytes(const std::filesystem::path& path, std::size_t limit, const std::string& label) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw Error("could not inspect " + label + " file");
    if (size > limit) throw Error(label + " exceeds its size limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("could not open " + label + " file");
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() > limit) throw Error(label + " exceeds its size limit");
    if (!input.eof() && input.fail()) throw Error("could not read " + label + " file");
    return bytes;
}

} // namespace

void validate_report(const Json& report) {
    keys(report, {"schema_version", "report_id", "status", "experiment_id", "application_name",
                  "baseline", "candidate", "environment_differences", "comparison_profile",
                  "coverage", "findings", "uncertainties", "summary"}, "report");
    if (need(report, "schema_version", "report") != 1) throw Error("report.schema_version must equal 1");
    digest(need(report, "report_id", "report"), "report.report_id");
    enum_text(need(report, "status", "report"), {"complete", "partial"}, "report.status");
    text(need(report, "experiment_id", "report"), "report.experiment_id", 1, 128);
    text(need(report, "application_name", "report"), "report.application_name", 1, 256);
    validate_run(need(report, "baseline", "report"), "report.baseline");
    validate_run(need(report, "candidate", "report"), "report.candidate");

    const auto baseline_run = report.at("baseline").at("run_id").get<std::string>();
    const auto candidate_run = report.at("candidate").at("run_id").get<std::string>();
    if (baseline_run == candidate_run) throw Error("baseline and candidate must reference distinct runs");

    const auto& differences = need(report, "environment_differences", "report");
    array(differences, "report.environment_differences");
    for (const auto& difference : differences) {
        keys(difference, {"field", "baseline_value", "candidate_value"}, "environment difference");
        text(need(difference, "field", "environment difference"), "environment difference.field", 1, 128);
        need(difference, "baseline_value", "environment difference");
        need(difference, "candidate_value", "environment difference");
    }

    const auto& findings = need(report, "findings", "report");
    array(findings, "report.findings");
    for (const auto& finding : findings) validate_finding(finding, baseline_run, candidate_run);

    const auto& uncertainties = need(report, "uncertainties", "report");
    array(uncertainties, "report.uncertainties");
    std::set<std::pair<std::string, std::string>> uncertainty_pairs;
    for (const auto& item : uncertainties) {
        keys(item, {"code", "message", "run_id"}, "uncertainty");
        const auto code = text(need(item, "code", "uncertainty"), "uncertainty.code", 1, 128);
        text(need(item, "message", "uncertainty"), "uncertainty.message", 1, 2048);
        const auto& run_id = need(item, "run_id", "uncertainty");
        if (!run_id.is_null()) text(run_id, "uncertainty.run_id", 1, 128);
        uncertainty_pairs.emplace(code, run_id.is_null() ? "" : run_id.get<std::string>());
    }

    const auto& summary = need(report, "summary", "report");
    keys(summary, {"baseline_event_count", "candidate_event_count", "baseline_behaviour_event_count",
                   "candidate_behaviour_event_count", "added_count", "removed_count", "changed_count",
                   "uncertainty_count"}, "report.summary");
    for (const auto& name : {"baseline_event_count", "candidate_event_count", "baseline_behaviour_event_count",
                             "candidate_behaviour_event_count", "added_count", "removed_count",
                             "changed_count", "uncertainty_count"}) {
        count(need(summary, name, "report.summary"), std::string("report.summary.") + name);
    }
    if (summary.at("uncertainty_count") != uncertainties.size()) throw Error("summary.uncertainty_count does not match uncertainties");

    std::map<std::string, std::uint64_t> change_counts{{"added", 0}, {"removed", 0}, {"changed", 0}};
    for (const auto& finding : findings) ++change_counts[finding.at("change_type").get<std::string>()];
    for (const auto& [change, total] : change_counts) {
        if (summary.at(change + "_count") != total) throw Error("summary." + change + "_count does not match findings");
    }
    if (summary.at("baseline_behaviour_event_count") > summary.at("baseline_event_count")
        || summary.at("candidate_behaviour_event_count") > summary.at("candidate_event_count")) {
        throw Error("behaviour event count exceeds total event count");
    }

    for (const auto& role : {"baseline", "candidate"}) {
        const auto& run = report.at(role);
        if (run.at("capture_status") != "completed"
            && !uncertainty_pairs.contains({"capture_not_completed", run.at("run_id").get<std::string>()})) {
            throw Error(std::string(role) + " capture status requires a capture_not_completed uncertainty");
        }
    }

    if (has(report, "comparison_profile")) {
        const auto& profile = report.at("comparison_profile");
        keys(profile, {"profile_id", "sha256", "included_categories", "ignored_attributes"}, "comparison_profile");
        const auto profile_id = text(need(profile, "profile_id", "comparison_profile"), "comparison_profile.profile_id", 1, 64);
        digest(need(profile, "sha256", "comparison_profile"), "comparison_profile.sha256");
        (void)profile_id;
        const auto included = string_set(need(profile, "included_categories", "comparison_profile"),
                                         kCategories, "comparison_profile.included_categories", true);
        const auto ignored_value = need(profile, "ignored_attributes", "comparison_profile");
        array(ignored_value, "comparison_profile.ignored_attributes");
        if (ignored_value.size() > 64U) throw Error("comparison_profile.ignored_attributes exceeds 64 items");
        std::set<std::string> ignored;
        for (const auto& item : ignored_value) {
            const auto key = text(item, "comparison_profile ignored attribute", 1, 128);
            if (!ignored.insert(key).second) throw Error("comparison_profile.ignored_attributes contains duplicates");
        }
        std::vector<std::string> included_sorted(included.begin(), included.end());
        std::vector<std::string> ignored_sorted(ignored.begin(), ignored.end());
        if (profile.at("included_categories") != included_sorted || ignored_value != ignored_sorted) {
            throw Error("comparison_profile lists must be normalized in sorted order");
        }
        const Json profile_core{
            {"schema_version", 1},
            {"profile_id", profile_id},
            {"included_categories", included_sorted},
            {"ignored_attributes", ignored_sorted}
        };
        if (sha256_hex(canonical_json(profile_core)) != profile.at("sha256")) {
            throw Error("comparison_profile.sha256 does not match its configuration");
        }
    }

    if (has(report, "coverage")) {
        const auto& coverage = report.at("coverage");
        keys(coverage, {"baseline", "candidate"}, "report.coverage");
        for (const auto& role : {"baseline", "candidate"}) validate_coverage(need(coverage, role, "report.coverage"), role);

        for (const auto& role : {"baseline", "candidate"}) {
            const auto& run = report.at(role);
            const auto& snapshot = coverage.at(role);
            const auto unavailable_values = string_set(snapshot.at("unavailable_categories"), kCoverageCategories, "unavailable categories");
            std::set<std::string> active;
            bool inactive = false;
            for (const auto& source : snapshot.at("event_sources")) {
                if (source.at("state") == "active") {
                    for (const auto& category : source.at("categories")) active.insert(category.get<std::string>());
                } else {
                    inactive = true;
                }
            }
            std::set<std::string> expected;
            if (run.at("capture_status") != "completed") expected.insert("capture_not_completed");
            if (!unavailable_values.empty()) expected.insert("categories_unavailable");
            if (inactive) expected.insert("event_sources_not_active");
            if (count(snapshot.at("dropped_events"), "dropped_events") > 0) expected.insert("events_dropped");
            if (!snapshot.at("errors").empty()) expected.insert("collection_errors");
            for (const auto& category : kCoverageCategories) {
                if (!active.contains(category) && !unavailable_values.contains(category)) expected.insert("coverage_not_declared");
            }
            if (run.at("artifact_sha256").is_null()) expected.insert("artifact_hash_missing");

            std::set<std::string> actual;
            for (const auto& [code, run_id] : uncertainty_pairs) {
                if (run_id == run.at("run_id").get<std::string>() && kUncertaintyCodes.contains(code)) actual.insert(code);
            }
            if (actual != expected) throw Error(std::string(role) + " coverage snapshot does not match uncertainty codes");

            std::uint64_t total = 0;
            for (const auto& key : {"process", "network", "file", "system", "coverage"}) {
                total += count(snapshot.at("recorded_event_counts").at(key), "recorded_event_counts");
            }
            if (total != count(summary.at(std::string(role) + "_event_count"), "event_count")) {
                throw Error(std::string(role) + " coverage event count does not match summary");
            }
            if (has(report, "comparison_profile")) {
                std::uint64_t behaviour = 0;
                for (const auto& category : report.at("comparison_profile").at("included_categories")) {
                    behaviour += count(snapshot.at("recorded_event_counts").at(category.get<std::string>()), "behaviour event count");
                }
                if (behaviour != count(summary.at(std::string(role) + "_behaviour_event_count"), "behaviour event count")) {
                    throw Error(std::string(role) + " behaviour event count does not match profile coverage counts");
                }
            }
        }
    }

    const auto expected_status = uncertainties.empty() ? "complete" : "partial";
    if (report.at("status") != expected_status) throw Error("report.status does not match uncertainty count");

    Json core = report;
    core.erase("report_id");
    if (sha256_hex(canonical_json(core)) != report.at("report_id")) {
        throw Error("report_id does not match report content");
    }
}

Json analyze_repeat_reports(const std::vector<Json>& input_reports) {
    if (input_reports.size() < 2U) throw Error("repeat analysis requires at least two comparison reports");
    if (input_reports.size() > 500U) throw Error("repeat analysis accepts at most 500 reports");
    std::vector<Json> reports = input_reports;
    for (const auto& report : reports) validate_report(report);
    std::sort(reports.begin(), reports.end(), [](const Json& left, const Json& right) {
        return left.at("report_id").get<std::string>() < right.at("report_id").get<std::string>();
    });

    std::set<std::string> report_ids;
    for (const auto& report : reports) {
        const auto id = report.at("report_id").get<std::string>();
        if (!report_ids.insert(id).second) throw Error("repeat analysis cannot contain the same report more than once");
    }
    const Json cohort = cohort_for(reports.front());
    for (const auto& report : reports) {
        if (cohort_for(report) != cohort) {
            throw Error("all reports must use the same application, versions, artifact hashes, and comparison profile");
        }
    }

    struct PatternData {
        Json exemplar;
        std::map<std::string, std::set<std::string>> references;
    };
    std::map<std::string, PatternData> patterns;
    Json source_reports = Json::array();
    Json partial_ids = Json::array();

    for (const auto& report : reports) {
        const auto id = report.at("report_id").get<std::string>();
        if (report.at("status") == "partial") partial_ids.push_back(id);
        const auto& summary = report.at("summary");
        const auto& findings = report.at("findings");
        const auto& uncertainties = report.at("uncertainties");
        source_reports.push_back(Json{
            {"report_id", id},
            {"status", report.at("status")},
            {"added_count", summary.at("added_count")},
            {"removed_count", summary.at("removed_count")},
            {"changed_count", summary.at("changed_count")},
            {"uncertainty_count", uncertainties.size()}
        });

        std::set<std::string> ignored;
        if (has(report, "comparison_profile")) {
            for (const auto& item : report.at("comparison_profile").at("ignored_attributes")) {
                ignored.insert(item.get<std::string>());
            }
        }
        for (const auto& finding : findings) {
            const auto fingerprint = finding_pattern(finding, ignored);
            auto& pattern = patterns[fingerprint];
            pattern.references[id].insert(finding.at("finding_id").get<std::string>());
            if (pattern.exemplar.is_null()) {
                pattern.exemplar = Json{
                    {"rule_id", finding.at("rule_id")},
                    {"change_type", finding.at("change_type")},
                    {"category", finding.at("category")},
                    {"summary", finding.at("summary")},
                    {"observation", finding.at("observation")}
                };
            }
        }
    }

    Json output_patterns = Json::array();
    std::uint64_t recurring = 0;
    for (const auto& [fingerprint, data] : patterns) {
        Json refs = Json::array();
        for (const auto& [report_id, finding_ids] : data.references) {
            refs.push_back(Json{
                {"report_id", report_id},
                {"finding_ids", std::vector<std::string>(finding_ids.begin(), finding_ids.end())}
            });
        }
        const auto occurrence = data.references.size();
        const bool is_recurring = occurrence == reports.size();
        if (is_recurring) ++recurring;
        Json pattern = data.exemplar;
        pattern["fingerprint"] = fingerprint;
        pattern["occurrence_count"] = occurrence;
        pattern["repeatability"] = is_recurring ? "recurring" : "not_consistently_observed";
        pattern["report_refs"] = std::move(refs);
        output_patterns.push_back(std::move(pattern));
    }

    const auto not_consistent = output_patterns.size() - recurring;
    Json core{
        {"schema_version", 1},
        {"cohort", cohort},
        {"report_count", reports.size()},
        {"reports", source_reports},
        {"partial_report_ids", partial_ids},
        {"patterns", output_patterns},
        {"summary", {
            {"recurring_pattern_count", recurring},
            {"not_consistently_observed_pattern_count", not_consistent},
            {"partial_report_count", partial_ids.size()}
        }}
    };
    Json result = core;
    result["analysis_id"] = sha256_hex(canonical_json(core));
    return result;
}

void validate_repeat_analysis(const Json& analysis) {
    keys(analysis, {"schema_version", "analysis_id", "cohort", "report_count", "reports",
                    "partial_report_ids", "patterns", "summary"}, "repeat analysis");
    if (need(analysis, "schema_version", "repeat analysis") != 1) throw Error("repeat analysis schema_version must equal 1");
    digest(need(analysis, "analysis_id", "repeat analysis"), "analysis_id");
    const auto reports_count = count(need(analysis, "report_count", "repeat analysis"), "report_count");
    if (reports_count < 2 || reports_count > 500) throw Error("repeat analysis report_count must be between 2 and 500");
    const auto& reports = need(analysis, "reports", "repeat analysis");
    array(reports, "repeat analysis.reports");
    if (reports.size() != reports_count) throw Error("report_count does not match source reports");
    std::set<std::string> ids;
    std::set<std::string> partial;
    for (const auto& report : reports) {
        keys(report, {"report_id", "status", "added_count", "removed_count", "changed_count", "uncertainty_count"}, "source report");
        digest(need(report, "report_id", "source report"), "source report.report_id");
        const auto id = report.at("report_id").get<std::string>();
        if (!ids.insert(id).second) throw Error("source report IDs must be unique");
        enum_text(need(report, "status", "source report"), {"complete", "partial"}, "source report.status");
        for (const auto& field : {"added_count", "removed_count", "changed_count", "uncertainty_count"}) {
            count(need(report, field, "source report"), std::string("source report.") + field);
        }
        if (report.at("status") == "partial") partial.insert(id);
    }
    const auto& partial_values = need(analysis, "partial_report_ids", "repeat analysis");
    array(partial_values, "partial_report_ids");
    if (partial_values != std::vector<std::string>(partial.begin(), partial.end())) throw Error("partial_report_ids do not match source report statuses");

    const auto& patterns = need(analysis, "patterns", "repeat analysis");
    array(patterns, "repeat analysis.patterns");
    std::uint64_t recurring = 0;
    for (const auto& pattern : patterns) {
        keys(pattern, {"fingerprint", "rule_id", "change_type", "category", "summary", "observation",
                       "occurrence_count", "repeatability", "report_refs"}, "repeat pattern");
        digest(need(pattern, "fingerprint", "pattern"), "pattern.fingerprint");
        enum_text(need(pattern, "rule_id", "pattern"), {"DG-COMPARE-001", "DG-COMPARE-002"}, "pattern.rule_id");
        enum_text(need(pattern, "change_type", "pattern"), {"added", "removed", "changed"}, "pattern.change_type");
        enum_text(need(pattern, "category", "pattern"), kCategories, "pattern.category");
        text(need(pattern, "summary", "pattern"), "pattern.summary", 1, 1024);
        text(need(pattern, "observation", "pattern"), "pattern.observation", 1, 2048);
        const auto occurrence = count(need(pattern, "occurrence_count", "pattern"), "pattern.occurrence_count");
        const auto label = text(need(pattern, "repeatability", "pattern"), "pattern.repeatability", 1);
        enum_text(pattern.at("repeatability"), {"recurring", "not_consistently_observed"}, "pattern.repeatability");
        const auto& refs = need(pattern, "report_refs", "pattern");
        array(refs, "pattern.report_refs");
        if (refs.empty() || occurrence != refs.size()) throw Error("pattern occurrence_count does not match report_refs");
        std::set<std::string> referenced;
        for (const auto& ref : refs) {
            keys(ref, {"report_id", "finding_ids"}, "pattern report reference");
            digest(need(ref, "report_id", "pattern report reference"), "reference.report_id");
            const auto id = ref.at("report_id").get<std::string>();
            if (!ids.contains(id) || !referenced.insert(id).second) throw Error("pattern reference contains unknown or duplicate report_id");
            const auto& finding_ids = need(ref, "finding_ids", "pattern report reference");
            array(finding_ids, "reference.finding_ids");
            if (finding_ids.empty()) throw Error("pattern reference finding_ids must not be empty");
            std::set<std::string> seen_findings;
            for (const auto& finding_id : finding_ids) {
                digest(finding_id, "reference finding_id");
                if (!seen_findings.insert(finding_id.get<std::string>()).second) throw Error("duplicate finding_id in pattern reference");
            }
        }
        const bool should_recur = occurrence == reports_count;
        if (label != (should_recur ? "recurring" : "not_consistently_observed")) throw Error("pattern repeatability label does not match occurrence count");
        if (should_recur) ++recurring;
    }

    const auto& summary = need(analysis, "summary", "repeat analysis");
    keys(summary, {"recurring_pattern_count", "not_consistently_observed_pattern_count", "partial_report_count"}, "repeat analysis.summary");
    if (count(need(summary, "recurring_pattern_count", "summary"), "recurring_pattern_count") != recurring
        || count(need(summary, "not_consistently_observed_pattern_count", "summary"), "not_consistently_observed_pattern_count") != patterns.size()
            - recurring
        || count(need(summary, "partial_report_count", "summary"), "partial_report_count") != partial.size()) {
        throw Error("repeat analysis summary does not match its reports and patterns");
    }
    Json core = analysis;
    core.erase("analysis_id");
    if (sha256_hex(canonical_json(core)) != analysis.at("analysis_id")) throw Error("analysis_id does not match repeat-analysis content");
}

Json attest_report(const std::filesystem::path& report_path) {
    const auto raw = read_bytes(report_path, kReportLimit, "comparison report");
    const Json report = parse_json_strict(raw);
    validate_report(report);
    return Json{
        {"schema_version", 1},
        {"artifact_type", "driftglass-comparison-report"},
        {"artifact_sha256", sha256_hex(raw)},
        {"artifact_size_bytes", raw.size()},
        {"report_id", report.at("report_id")}
    };
}

Json verify_report_integrity(const std::filesystem::path& report_path, const std::filesystem::path& manifest_path) {
    const auto raw = read_bytes(report_path, kReportLimit, "comparison report");
    const Json report = parse_json_strict(raw);
    validate_report(report);
    const Json manifest = parse_json_strict(read_bytes(manifest_path, kIntegrityLimit, "integrity manifest"));
    keys(manifest, {"schema_version", "artifact_type", "artifact_sha256", "artifact_size_bytes", "report_id"}, "integrity manifest");
    if (need(manifest, "schema_version", "manifest") != 1) throw Error("integrity manifest schema_version must equal 1");
    if (need(manifest, "artifact_type", "manifest") != "driftglass-comparison-report") throw Error("integrity manifest artifact_type is invalid");
    digest(need(manifest, "artifact_sha256", "manifest"), "manifest.artifact_sha256");
    digest(need(manifest, "report_id", "manifest"), "manifest.report_id");
    const auto size = count(need(manifest, "artifact_size_bytes", "manifest"), "manifest.artifact_size_bytes");
    if (size == 0 || size > kReportLimit) throw Error("integrity manifest artifact_size_bytes is outside the allowed range");
    if (manifest.at("artifact_sha256") != sha256_hex(raw)) throw Error("report SHA-256 does not match integrity manifest");
    if (size != raw.size()) throw Error("report size does not match integrity manifest");
    if (manifest.at("report_id") != report.at("report_id")) throw Error("report_id does not match integrity manifest");
    return manifest;
}

} // namespace driftglass
