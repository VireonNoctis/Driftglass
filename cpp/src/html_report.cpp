#include "driftglass/core.hpp"

#include <sstream>
#include <string>

namespace driftglass {
namespace {

std::string html_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

std::string display_json(const Json& value) {
    if (value.is_null()) return "—";
    if (value.is_string()) return value.get<std::string>();
    return value.dump(2, ' ', true);
}

std::string evidence_html(const char* title, const Json& evidence) {
    std::ostringstream out;
    if (evidence.empty()) {
        out << "<p class=\"empty\">" << html_escape(title) << ": no matching evidence in this run.</p>";
        return out.str();
    }
    for (const auto& item : evidence) {
        out << "<details><summary>" << html_escape(title) << ": "
            << html_escape(item.at("event_id").get<std::string>()) << "</summary><pre>"
            << html_escape(item.dump(2, ' ', true)) << "</pre></details>";
    }
    return out.str();
}

std::string coverage_html(const Json& report) {
    if (!report.contains("coverage")) {
        return "<p class=\"empty\">This older report does not include a per-run coverage snapshot. Its uncertainty items remain available below.</p>";
    }
    std::ostringstream out;
    const auto& coverage = report.at("coverage");
    for (const auto* role : {"baseline", "candidate"}) {
        const auto& run = coverage.at(role);
        out << "<h3>" << (std::string(role) == "baseline" ? "Baseline" : "Candidate") << "</h3>";
        out << "<p>Declared unavailable categories: ";
        if (run.at("unavailable_categories").empty()) out << "None declared";
        else {
            bool first = true;
            for (const auto& item : run.at("unavailable_categories")) {
                if (!first) out << ", ";
                first = false;
                out << html_escape(item.get<std::string>());
            }
        }
        out << "</p><p>Dropped-event count reported by collector: "
            << run.at("dropped_events") << "</p>";
        out << "<table><thead><tr><th>Event source</th><th>Declared state</th><th>Categories</th></tr></thead><tbody>";
        if (run.at("event_sources").empty()) {
            out << "<tr><td colspan=\"3\">No event sources were declared.</td></tr>";
        } else {
            for (const auto& source : run.at("event_sources")) {
                out << "<tr><th>" << html_escape(source.at("name").get<std::string>())
                    << "</th><td>" << html_escape(source.at("state").get<std::string>())
                    << "</td><td>";
                bool first = true;
                for (const auto& category : source.at("categories")) {
                    if (!first) out << ", ";
                    first = false;
                    out << html_escape(category.get<std::string>());
                }
                if (first) out << "No categories declared";
                out << "</td></tr>";
            }
        }
        out << "</tbody></table><table><thead><tr><th>Category in supplied event stream</th><th>Input records</th></tr></thead><tbody>";
        for (const auto& [category, count] : run.at("recorded_event_counts").items()) {
            out << "<tr><th>" << html_escape(category) << "</th><td>" << count << "</td></tr>";
        }
        out << "</tbody></table><h4>Declared collection errors</h4><ul>";
        if (run.at("errors").empty()) out << "<li>No collection errors declared in the manifest.</li>";
        else for (const auto& error : run.at("errors")) out << "<li>" << html_escape(error.get<std::string>()) << "</li>";
        out << "</ul>";
    }
    return out.str();
}

std::string profile_html(const Json& report) {
    if (!report.contains("comparison_profile")) {
        return "<p class=\"empty\">This older report does not include comparison-profile metadata.</p>";
    }
    const auto& profile = report.at("comparison_profile");
    auto join = [](const Json& values) {
        std::string result;
        for (const auto& item : values) {
            if (!result.empty()) result += ", ";
            result += item.get<std::string>();
        }
        return result.empty() ? std::string("None") : result;
    };
    std::ostringstream out;
    out << "<p><strong>Profile:</strong> " << html_escape(profile.at("profile_id").get<std::string>()) << "</p>"
        << "<p><strong>Profile SHA-256:</strong> <code>" << html_escape(profile.at("sha256").get<std::string>()) << "</code></p>"
        << "<p><strong>Included categories:</strong> " << html_escape(join(profile.at("included_categories"))) << "</p>"
        << "<p><strong>Ignored attributes for matching:</strong> " << html_escape(join(profile.at("ignored_attributes"))) << "</p>";
    return out.str();
}

} // namespace

std::string render_html_report(const Json& report) {
    validate_report(report);
    const auto& summary = report.at("summary");
    const auto& baseline = report.at("baseline");
    const auto& candidate = report.at("candidate");
    std::ostringstream out;

    out << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        << "<title>Driftglass report — " << html_escape(report.at("application_name").get<std::string>()) << "</title>"
        << R"HTML(<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#08090b;color:#f0f1f2}
body{margin:0 auto;max-width:1100px;padding:2rem 1.2rem 4rem;line-height:1.5}
h1,h2,h3{line-height:1.2}h1{margin-bottom:.35rem;font-size:clamp(2rem,5vw,3.6rem);letter-spacing:-.05em}
h2{margin-top:2.2rem;padding-bottom:.55rem;border-bottom:1px solid #3b4047}
p{max-width:85ch}.meta,.id{color:#aeb4ba;font:.82rem ui-monospace,monospace;overflow-wrap:anywhere}
.id{font-size:.75rem}.status{display:inline-block;border:1px solid #737980;padding:.2rem .55rem;text-transform:uppercase;letter-spacing:.08em;font-size:.75rem}
table{border-collapse:collapse;width:100%;margin:.7rem 0}
th,td{border-bottom:1px solid #30353c;padding:.55rem .6rem;text-align:left;vertical-align:top;overflow-wrap:anywhere}
th{color:#bdc3c9;font-weight:600}article.finding{border-top:1px solid #43474b;padding:1rem 0 1.5rem}
.interpretation,.empty{color:#c2c7cc}details{margin:.5rem 0;border:1px solid #3b4044;padding:.4rem .6rem}
summary{cursor:pointer}pre{overflow-x:auto;white-space:pre-wrap;overflow-wrap:anywhere;font:.8rem ui-monospace,monospace}
li{margin:.6rem 0}a{color:inherit}
</style></head><body>)HTML";

    out << "<header><p class=\"meta\">DRIFTGLASS · REPORT SCHEMA V1</p><h1>"
        << html_escape(report.at("application_name").get<std::string>())
        << "</h1><p class=\"status\">" << html_escape(report.at("status").get<std::string>())
        << "</p><p>Report ID <span class=\"id\">" << html_escape(report.at("report_id").get<std::string>())
        << "</span></p><p>These findings describe observed differences in supplied records. They do not prove intent or establish that the application is safe.</p></header>";

    out << "<h2>Compared runs</h2><table><thead><tr><th>Role</th><th>Version</th><th>Run ID</th><th>Capture status</th></tr></thead><tbody>";
    for (const auto& pair : {std::pair<const char*, const Json*>{"Baseline", &baseline},
                             std::pair<const char*, const Json*>{"Candidate", &candidate}}) {
        out << "<tr><th>" << pair.first << "</th><td>" << html_escape(pair.second->at("version").get<std::string>())
            << "</td><td class=\"id\">" << html_escape(pair.second->at("run_id").get<std::string>())
            << "</td><td>" << html_escape(pair.second->at("capture_status").get<std::string>()) << "</td></tr>";
    }
    out << "</tbody></table>";

    out << "<h2>Summary</h2><table><tbody>";
    const std::vector<std::pair<std::string, std::string>> summary_fields{
        {"baseline_event_count", "Baseline input records"},
        {"candidate_event_count", "Candidate input records"},
        {"baseline_behaviour_event_count", "Baseline behaviour records"},
        {"candidate_behaviour_event_count", "Candidate behaviour records"},
        {"added_count", "Added findings"}, {"removed_count", "Removed findings"},
        {"changed_count", "Changed findings"}, {"uncertainty_count", "Uncertainty items"}
    };
    for (const auto& [key, label] : summary_fields) {
        out << "<tr><th>" << html_escape(label) << "</th><td>" << summary.at(key) << "</td></tr>";
    }
    out << "</tbody></table>";

    out << "<h2>Comparison profile</h2>" << profile_html(report);
    out << "<h2>Declared collection coverage</h2><p>Source states and input record counts are separate. Zero input records do not prove that no activity occurred. Older report-v1 files may not include this coverage snapshot.</p>"
        << coverage_html(report);

    out << "<h2>Run context differences</h2><table><thead><tr><th>Field</th><th>Baseline</th><th>Candidate</th></tr></thead><tbody>";
    if (report.at("environment_differences").empty()) {
        out << "<tr><td colspan=\"3\">No declared context differences.</td></tr>";
    } else {
        for (const auto& item : report.at("environment_differences")) {
            out << "<tr><th>" << html_escape(item.at("field").get<std::string>())
                << "</th><td>" << html_escape(display_json(item.at("baseline_value")))
                << "</td><td>" << html_escape(display_json(item.at("candidate_value"))) << "</td></tr>";
        }
    }
    out << "</tbody></table>";

    out << "<h2>Findings</h2>";
    if (report.at("findings").empty()) {
        out << "<p class=\"empty\">No differences were observed in the supplied behavioural records. This is limited to the input coverage.</p>";
    }
    for (const auto& finding : report.at("findings")) {
        out << "<article class=\"finding\"><p class=\"meta\">"
            << html_escape(finding.at("rule_id").get<std::string>()) << " · "
            << html_escape(finding.at("category").get<std::string>()) << " · "
            << html_escape(finding.at("change_type").get<std::string>())
            << "</p><h3>" << html_escape(finding.at("summary").get<std::string>())
            << "</h3><p>" << html_escape(finding.at("observation").get<std::string>())
            << "</p><p class=\"interpretation\">" << html_escape(finding.at("interpretation").get<std::string>())
            << "</p><p class=\"id\">Finding " << html_escape(finding.at("finding_id").get<std::string>()) << "</p>"
            << evidence_html("Baseline evidence", finding.at("baseline_evidence"))
            << evidence_html("Candidate evidence", finding.at("candidate_evidence")) << "</article>";
    }

    out << "<h2>Uncertainty and coverage gaps</h2><ul>";
    if (report.at("uncertainties").empty()) {
        out << "<li>No gaps were declared in the validated inputs. This is not a safety verdict.</li>";
    } else {
        for (const auto& item : report.at("uncertainties")) {
            out << "<li><strong>" << html_escape(item.at("code").get<std::string>())
                << "</strong>: " << html_escape(item.at("message").get<std::string>());
            if (!item.at("run_id").is_null()) out << " <span class=\"id\">Run "
                << html_escape(item.at("run_id").get<std::string>()) << "</span>";
            out << "</li>";
        }
    }
    out << "</ul><footer><p class=\"meta\">Generated locally from a validated report object. Review metadata before sharing; paths, hostnames, and event attributes can be sensitive.</p></footer></body></html>\n";
    return out.str();
}

} // namespace driftglass
