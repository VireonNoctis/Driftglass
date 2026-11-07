#include "driftglass/core.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>

namespace fs = std::filesystem;
using driftglass::Error;
using driftglass::Json;

namespace {

void print_usage() {
    std::cout
        << "Driftglass C++ comparison core\n\n"
        << "Usage:\n"
        << "  driftglass-core events validate <events.jsonl>\n"
        << "  driftglass-core manifest validate <manifest.json>\n"
        << "  driftglass-core capabilities\n"
        << "  driftglass-core report validate <report.json>\n"
        << "  driftglass-core report attest --report FILE --output FILE [--overwrite]\n"
        << "  driftglass-core report verify --report FILE --manifest FILE\n"
        << "  driftglass-core report redact --report FILE --output FILE [--paths] [--network] [--process-names]\n"
        << "  driftglass-core repeat analyze REPORT.json REPORT.json [REPORT.json ...] [--output FILE] [--overwrite]\n"
        << "  driftglass-core capture --observer FILE --pid PID --experiment-id ID --role baseline|candidate\n"
        << "      --application-name NAME --application-version VERSION --scenario TEXT\n"
        << "      --event-output FILE --manifest-output FILE [--duration-seconds N] [--interval-ms N]\n"
        << "  driftglass-core history list --database FILE [--limit N]\n"
        << "  driftglass-core history show --database FILE --report-id SHA256\n"
        << "  driftglass-core history export --database FILE --report-id SHA256 --format json|html --output FILE [--paths] [--network] [--process-names] [--overwrite]\n"
        << "  driftglass-core history delete --database FILE --report-id SHA256 --confirm SHA256\n"
        << "  driftglass-core compare --baseline-manifest FILE --candidate-manifest FILE\n"
        << "      --baseline-events FILE --candidate-events FILE [--profile FILE]\n"
        << "      [--output FILE] [--html-output FILE] [--history-db FILE] [--paths] [--network] [--process-names] [--overwrite]\n\n"
        << "The compare command reads existing captures. It never launches the application.\n";
}

std::map<std::string, std::string> parse_options(int argc, char** argv, int start) {
    const std::set<std::string> value_options{
        "--baseline-manifest", "--candidate-manifest",
        "--baseline-events", "--candidate-events",
        "--profile", "--output", "--report", "--manifest", "--history-db", "--database", "--report-id", "--confirm", "--limit", "--format", "--html-output", "--observer", "--pid", "--duration-seconds", "--interval-ms", "--experiment-id", "--role", "--application-name", "--application-version", "--scenario", "--event-output", "--manifest-output", "--artifact-sha256", "--package-id", "--profile", "--snapshot-id", "--event-schema", "--manifest-schema", "--report-schema", "--analysis-schema", "--profile-schema"
    };
    const std::set<std::string> flags{"--overwrite", "--paths", "--network", "--process-names"};
    std::map<std::string, std::string> result;
    for (int index = start; index < argc; ++index) {
        const std::string option = argv[index];
        if (flags.contains(option)) {
            if (result.contains(option)) throw Error("option " + option + " was supplied more than once");
            result.emplace(option, "true");
        } else if (value_options.contains(option)) {
            if (result.contains(option)) throw Error("option " + option + " was supplied more than once");
            if (index + 1 >= argc) throw Error("option " + option + " requires a value");
            result.emplace(option, argv[++index]);
        } else {
            throw Error("unknown option: " + option);
        }
    }
    return result;
}

const std::string& need(const std::map<std::string, std::string>& options, const std::string& key) {
    const auto found = options.find(key);
    if (found == options.end() || found->second.empty()) throw Error("missing required option " + key);
    return found->second;
}

fs::path normalized_path(const std::string& value) {
    std::error_code error;
    auto path = fs::absolute(fs::path(value), error);
    if (error) throw Error("cannot resolve an output/input path");
    auto parent = fs::weakly_canonical(path.parent_path(), error);
    if (error) throw Error("cannot resolve an output/input path");
    return (parent / path.filename()).lexically_normal();
}

void write_output(const fs::path& path, const std::string& content, bool overwrite) {
    const auto target = normalized_path(path.string());
    if (!fs::exists(target.parent_path())) throw Error("output directory does not exist");
    if (fs::is_directory(target)) throw Error("output path is a directory");
    if (!overwrite && fs::exists(target)) throw Error("output already exists; pass --overwrite to replace it");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = target;
    temporary += ".tmp-" + std::to_string(nonce);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output) throw Error("could not create temporary report output");
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.put('\n');
        output.flush();
        if (!output) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw Error("could not write report output");
        }
    }

    std::error_code error;
    if (overwrite) {
        fs::rename(temporary, target, error);
    } else {
        fs::create_hard_link(temporary, target, error);
        if (!error) fs::remove(temporary);
    }
    if (error) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        if (!overwrite && fs::exists(target)) throw Error("output already exists; no file was replaced");
        throw Error("could not publish report output");
    }
}



fs::path default_schema_path(const std::string& relative, const std::string& executable) {
    std::vector<fs::path> candidates;
    if (const char* configured = std::getenv("DRIFTGLASS_SCHEMA_DIR"); configured && *configured) {
        candidates.emplace_back(fs::path(configured) / relative);
    }
#ifdef DRIFTGLASS_SCHEMA_SOURCE_DIR
    candidates.emplace_back(fs::path(DRIFTGLASS_SCHEMA_SOURCE_DIR) / relative);
#endif
    std::error_code error;
    const auto executable_path = fs::absolute(fs::path(executable), error);
    if (!error) {
        candidates.emplace_back(executable_path.parent_path().parent_path()
            / "share" / "driftglass" / "schemas" / relative);
    }
    candidates.emplace_back(fs::current_path() / "schemas" / relative);
    candidates.emplace_back(fs::current_path() / "share" / "driftglass" / "schemas" / relative);
    for (const auto& candidate : candidates) {
        if (fs::is_regular_file(candidate, error) && !error) {
            const auto resolved = fs::weakly_canonical(candidate, error);
            return error ? candidate : resolved;
        }
        error.clear();
    }
    return {};
}

fs::path selected_schema(
    const std::map<std::string, std::string>& options,
    const std::string& option,
    const std::string& relative,
    const std::string& executable
) {
    fs::path path;
    const auto supplied = options.find(option);
    if (supplied != options.end()) path = normalized_path(supplied->second);
    else path = default_schema_path(relative, executable);
    if (path.empty() || !fs::is_regular_file(path)) {
        throw Error("schema file could not be located; supply " + option + " explicitly");
    }
    return path;
}

std::uint32_t parse_u32(const std::string& value, const std::string& label, std::uint32_t minimum, std::uint32_t maximum) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed);
        if (consumed != value.size() || parsed < minimum || parsed > maximum) {
            throw Error(label + " is outside the allowed range");
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (const Error&) {
        throw;
    } catch (...) {
        throw Error(label + " must be an integer in the allowed range");
    }
}

int run(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
        print_usage();
        return argc < 2 ? 2 : 0;
    }

    const std::string command = argv[1];
    if (command == "capabilities") {
        if (argc != 2) throw Error("usage: driftglass-core capabilities");
        std::cout << driftglass::capabilities_report().dump(2, ' ', true) << "\n";
        return 0;
    }
    if (command == "events") {
        if (argc < 4 || std::string(argv[2]) != "validate") {
            throw Error("usage: driftglass-core events validate <events.jsonl> [--event-schema FILE]");
        }
        const auto options = parse_options(argc, argv, 4);
        const auto schema = selected_schema(options, "--event-schema", "events/event-v1.schema.json", argv[0]);
        const auto events = driftglass::load_jsonl(argv[3], schema);
        std::cout << "valid event stream: " << events.size() << " record(s)\n";
        return 0;
    }
    if (command == "manifest") {
        if (argc < 4 || std::string(argv[2]) != "validate") {
            throw Error("usage: driftglass-core manifest validate <manifest.json> [--manifest-schema FILE]");
        }
        const auto options = parse_options(argc, argv, 4);
        const auto schema = selected_schema(options, "--manifest-schema", "manifests/run-manifest-v1.schema.json", argv[0]);
        const auto manifest = driftglass::load_json_file(argv[3], 4U * 1024U * 1024U);
        driftglass::validate_json_schema_file(manifest, schema);
        driftglass::validate_manifest(manifest);
        std::cout << "valid run manifest: run_id=" << manifest.at("run_id")
                  << " role=" << manifest.at("role")
                  << " capture=" << manifest.at("capture").at("status")
                  << " dropped_events=" << manifest.at("coverage").at("dropped_events") << "\n";
        return 0;
    }
    if (command == "report") {
        if (argc < 3) throw Error("usage: driftglass-core report validate|attest|verify ...");
        const std::string operation = argv[2];
        if (operation == "validate") {
            if (argc < 4) throw Error("usage: driftglass-core report validate <report.json> [--report-schema FILE]");
            const auto options = parse_options(argc, argv, 4);
            const auto schema = selected_schema(options, "--report-schema", "reports/report-v1.schema.json", argv[0]);
            const auto report = driftglass::load_json_file(argv[3], 64U * 1024U * 1024U);
            driftglass::validate_json_schema_file(report, schema);
            driftglass::validate_report(report);
            std::cout << "valid report: report_id=" << report.at("report_id") << "\n";
            return 0;
        }
        const auto options = parse_options(argc, argv, 3);
        if (operation == "attest") {
            const auto report_path = normalized_path(need(options, "--report"));
            const auto output_path = normalized_path(need(options, "--output"));
            if (report_path == output_path) throw Error("integrity manifest output must not replace the report");
            const auto report_schema = selected_schema(options, "--report-schema", "reports/report-v1.schema.json", argv[0]);
            const auto export_schema = selected_schema(options, "--manifest-schema", "reports/export-manifest-v1.schema.json", argv[0]);
            const auto source_report = driftglass::load_json_file(report_path, 64U * 1024U * 1024U);
            driftglass::validate_json_schema_file(source_report, report_schema);
            driftglass::validate_report(source_report);
            const auto manifest = driftglass::attest_report(report_path);
            driftglass::validate_json_schema_file(manifest, export_schema);
            write_output(output_path, manifest.dump(2, ' ', true), options.contains("--overwrite"));
            std::cout << manifest.dump(2, ' ', true) << "\n";
            return 0;
        }
        if (operation == "redact") {
            const auto report_path = normalized_path(need(options, "--report"));
            const auto output_path = normalized_path(need(options, "--output"));
            if (report_path == output_path) throw Error("redacted output must not replace the source report");
            const auto report_schema = selected_schema(options, "--report-schema", "reports/report-v1.schema.json", argv[0]);
            const auto source_report = driftglass::load_json_file(report_path, 64U * 1024U * 1024U);
            driftglass::validate_json_schema_file(source_report, report_schema);
            driftglass::validate_report(source_report);
            const auto redacted = driftglass::redact_report(
                source_report, options.contains("--paths"), options.contains("--network"), options.contains("--process-names"));
            driftglass::validate_json_schema_file(redacted, report_schema);
            write_output(output_path, redacted.dump(2, ' ', true), options.contains("--overwrite"));
            std::cout << redacted.dump(2, ' ', true) << "\n";
            return 0;
        }
        if (operation == "verify") {
            if (options.contains("--output") || options.contains("--overwrite")) {
                throw Error("report verify does not accept output options");
            }
            const auto report_path = normalized_path(need(options, "--report"));
            const auto manifest_path = normalized_path(need(options, "--manifest"));
            const auto report_schema = selected_schema(options, "--report-schema", "reports/report-v1.schema.json", argv[0]);
            const auto export_schema = selected_schema(options, "--manifest-schema", "reports/export-manifest-v1.schema.json", argv[0]);
            const auto report = driftglass::load_json_file(report_path, 64U * 1024U * 1024U);
            const auto export_manifest = driftglass::load_json_file(manifest_path, 1024U * 1024U);
            driftglass::validate_json_schema_file(report, report_schema);
            driftglass::validate_json_schema_file(export_manifest, export_schema);
            const auto manifest = driftglass::verify_report_integrity(report_path, manifest_path);
            std::cout << manifest.dump(2, ' ', true) << "\n";
            std::cerr << "integrity_status=verified report_id=" << manifest.at("report_id") << "\n";
            return 0;
        }
        throw Error("unknown report operation: " + operation);
    }
    if (command == "repeat") {
        if (argc < 3 || std::string(argv[2]) != "analyze") {
            throw Error("usage: driftglass-core repeat analyze REPORT.json REPORT.json [REPORT.json ...]");
        }
        std::vector<std::filesystem::path> paths;
        std::optional<std::filesystem::path> output;
        std::map<std::string, std::string> schema_options;
        bool overwrite = false;
        for (int index = 3; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--overwrite") {
                if (overwrite) throw Error("--overwrite was supplied more than once");
                overwrite = true;
            } else if (argument == "--output") {
                if (output || index + 1 >= argc) throw Error("--output requires one value and may be supplied once");
                output = normalized_path(argv[++index]);
            } else if (argument == "--report-schema" || argument == "--analysis-schema") {
                if (index + 1 >= argc || schema_options.contains(argument)) {
                    throw Error(argument + " requires one value and may be supplied once");
                }
                schema_options.emplace(argument, argv[++index]);
            } else if (argument.starts_with("--")) {
                throw Error("unknown repeat option: " + argument);
            } else {
                paths.emplace_back(normalized_path(argument));
            }
        }
        if (paths.size() < 2U || paths.size() > 500U) throw Error("repeat analyze requires between 2 and 500 report files");
        if (overwrite && !output) throw Error("--overwrite requires --output");
        const auto report_schema = selected_schema(schema_options, "--report-schema", "reports/report-v1.schema.json", argv[0]);
        const auto analysis_schema = selected_schema(schema_options, "--analysis-schema", "reports/repeat-analysis-v1.schema.json", argv[0]);
        std::uint64_t total_bytes = 0;
        std::vector<Json> reports;
        reports.reserve(paths.size());
        for (const auto& path : paths) {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error) throw Error("could not inspect repeat-analysis report file");
            if (size > 64U * 1024U * 1024U) throw Error("a repeat-analysis report exceeds the 64 MiB file limit");
            total_bytes += size;
            if (total_bytes > 256U * 1024U * 1024U) throw Error("combined repeat-analysis input exceeds the 256 MiB limit");
            const auto report = driftglass::load_json_file(path, 64U * 1024U * 1024U);
            driftglass::validate_json_schema_file(report, report_schema);
            reports.push_back(report);
        }
        const auto analysis = driftglass::analyze_repeat_reports(reports);
        driftglass::validate_json_schema_file(analysis, analysis_schema);
        driftglass::validate_repeat_analysis(analysis);
        const auto serialized = analysis.dump(2, ' ', true);
        if (output) write_output(*output, serialized, overwrite);
        else std::cout << serialized << "\n";
        std::cerr << "analysis_id=" << analysis.at("analysis_id")
                  << " reports=" << analysis.at("report_count")
                  << " patterns=" << analysis.at("patterns").size() << "\n";
        return 0;
    }
    if (command == "capture") {
        const auto options = parse_options(argc, argv, 2);
        if (options.contains("--overwrite")) throw Error("capture outputs are never overwritten");
        driftglass::CaptureOptions capture_options;
        capture_options.observer_path = need(options, "--observer");
        capture_options.pid = parse_u32(need(options, "--pid"), "pid", 1, 2147483647U);
        capture_options.duration_seconds = options.contains("--duration-seconds")
            ? parse_u32(options.at("--duration-seconds"), "duration_seconds", 1, 3600) : 30U;
        capture_options.interval_ms = options.contains("--interval-ms")
            ? parse_u32(options.at("--interval-ms"), "interval_ms", 100, 10000) : 250U;
        capture_options.experiment_id = need(options, "--experiment-id");
        capture_options.role = need(options, "--role");
        capture_options.application_name = need(options, "--application-name");
        capture_options.application_version = need(options, "--application-version");
        capture_options.scenario = need(options, "--scenario");
        capture_options.event_output_path = need(options, "--event-output");
        capture_options.manifest_output_path = need(options, "--manifest-output");
        capture_options.artifact_sha256 = options.contains("--artifact-sha256")
            ? std::optional<std::string>(options.at("--artifact-sha256")) : std::nullopt;
        capture_options.package_id = options.contains("--package-id")
            ? std::optional<std::string>(options.at("--package-id")) : std::nullopt;
#if defined(__APPLE__)
        capture_options.profile = options.contains("--profile") ? options.at("--profile") : "macos-libproc-polling";
#elif defined(__linux__)
        capture_options.profile = options.contains("--profile") ? options.at("--profile") : "linux-procfs-polling";
#else
        capture_options.profile = options.contains("--profile") ? options.at("--profile") : "native-polling";
#endif
        capture_options.snapshot_id = options.contains("--snapshot-id")
            ? std::optional<std::string>(options.at("--snapshot-id")) : std::nullopt;
        const auto result = driftglass::capture_run(capture_options);
        const auto& manifest = result.manifest;
        std::cout << "capture_status=" << manifest.at("capture").at("status")
                  << " run_id=" << manifest.at("run_id")
                  << " role=" << manifest.at("role")
                  << " events=" << result.event_count
                  << " coverage_notes=" << manifest.at("coverage").at("errors").size()
                  << " manifest=" << result.manifest_path.string()
                  << " events_file=" << result.event_path.string() << "\n";
        for (const auto& message : manifest.at("coverage").at("errors")) {
            std::cerr << "coverage: " << message.get<std::string>() << "\n";
        }
        return result.completed ? 0 : 1;
    }
    if (command == "history") {
        if (argc < 3) throw Error("usage: driftglass-core history list|show|delete ...");
        const std::string operation = argv[2];
        const auto options = parse_options(argc, argv, 3);
        const auto database = normalized_path(need(options, "--database"));
        if (operation == "list") {
            const auto limit_text = options.contains("--limit") ? options.at("--limit") : "100";
            std::size_t limit = 0;
            try {
                std::size_t consumed = 0;
                const auto parsed = std::stoull(limit_text, &consumed);
                if (consumed != limit_text.size() || parsed < 1 || parsed > 1000U) throw Error("history limit must be between 1 and 1000");
                limit = static_cast<std::size_t>(parsed);
            } catch (const Error&) { throw; }
            catch (...) { throw Error("history limit must be an integer between 1 and 1000"); }
            const auto reports = driftglass::list_reports(database, limit);
            if (reports.empty()) {
                std::cout << "no reports in local history\n";
            } else {
                for (const auto& item : reports) {
                    std::cout << item.at("report_id") << "  " << item.at("status") << "  "
                              << item.at("application_name") << "  "
                              << item.at("baseline_version") << " -> " << item.at("candidate_version")
                              << "  findings=" << item.at("finding_count")
                              << " uncertainties=" << item.at("uncertainty_count") << "\n";
                }
            }
            return 0;
        }
        if (operation == "show") {
            const auto report = driftglass::get_report(database, need(options, "--report-id"));
            if (!report) throw Error("report not found in local history");
            std::cout << report->dump(2, ' ', true) << "\n";
            return 0;
        }
        if (operation == "export") {
            const auto report_id = need(options, "--report-id");
            const auto format = need(options, "--format");
            if (format != "json" && format != "html") {
                throw Error("history export format must be json or html");
            }
            const auto output = normalized_path(need(options, "--output"));
            if (output == database) {
                throw Error("history export output must not replace the history database");
            }
            const auto report = driftglass::get_report(database, report_id);
            if (!report) throw Error("report not found in local history");
            const bool redact_paths = options.contains("--paths");
            const bool redact_network = options.contains("--network");
            const bool redact_processes = options.contains("--process-names");
            const Json exported = (redact_paths || redact_network || redact_processes)
                ? driftglass::redact_report(*report, redact_paths, redact_network, redact_processes)
                : *report;
            const std::string serialized = format == "json"
                ? exported.dump(2, ' ', true, Json::error_handler_t::strict)
                : driftglass::render_html_report(exported);
            write_output(output, serialized, options.contains("--overwrite"));
            if (redact_paths || redact_network || redact_processes) {
                std::cout << "exported redacted report " << exported.at("report_id")
                          << " (source report " << report_id << ") to " << output.string() << "\n";
            } else {
                std::cout << "exported report " << report_id << " to " << output.string() << "\n";
            }
            return 0;
        }
        if (operation == "delete") {
            const auto report_id = need(options, "--report-id");
            if (need(options, "--confirm") != report_id) {
                throw Error("confirmation must exactly match --report-id; nothing was deleted");
            }
            if (!driftglass::delete_report(database, report_id)) throw Error("report not found in local history");
            std::cout << "deleted local report " << report_id
                      << "; unreferenced run rows removed; source capture files unchanged\n";
            return 0;
        }
        throw Error("unknown history operation: " + operation);
    }
    if (command == "compare") {
        const auto options = parse_options(argc, argv, 2);
        const auto baseline_manifest = need(options, "--baseline-manifest");
        const auto candidate_manifest = need(options, "--candidate-manifest");
        const auto baseline_events = need(options, "--baseline-events");
        const auto candidate_events = need(options, "--candidate-events");
        const auto profile = options.contains("--profile") ? options.at("--profile") : std::string{};
        const bool overwrite = options.contains("--overwrite");
        const bool store_history = options.contains("--history-db");

        if (overwrite && !options.contains("--output") && !options.contains("--html-output")) throw Error("--overwrite requires an output path");
        for (const auto* output_option : {"--output", "--html-output"}) {
            if (!options.contains(output_option)) continue;
            const auto output = normalized_path(options.at(output_option));
            for (const auto& input : {baseline_manifest, candidate_manifest, baseline_events, candidate_events}) {
                if (output == normalized_path(input)) throw Error("report output must not replace an input file");
            }
            if (!profile.empty() && output == normalized_path(profile)) throw Error("report output must not replace the rule profile");
        }
        if (options.contains("--output") && options.contains("--html-output")
            && normalized_path(options.at("--output")) == normalized_path(options.at("--html-output"))) {
            throw Error("JSON and HTML reports must use different output paths");
        }
        if (store_history) {
            const auto history = normalized_path(options.at("--history-db"));
            for (const auto& input : {baseline_manifest, candidate_manifest, baseline_events, candidate_events}) {
                if (history == normalized_path(input)) throw Error("history database must not replace an input file");
            }
            for (const auto* output_option : {"--output", "--html-output"}) {
                if (options.contains(output_option) && history == normalized_path(options.at(output_option))) {
                    throw Error("history database and report output must use different paths");
                }
            }
            if (!profile.empty() && history == normalized_path(profile)) throw Error("history database must not replace the rule profile");
        }

        const Json report = driftglass::compare_runs(
            baseline_manifest, candidate_manifest, baseline_events, candidate_events, profile);
        if (store_history) (void)driftglass::save_report(options.at("--history-db"), report);
        const bool redact_paths = options.contains("--paths");
        const bool redact_network = options.contains("--network");
        const bool redact_processes = options.contains("--process-names");
        const Json exported_report = (redact_paths || redact_network || redact_processes)
            ? driftglass::redact_report(report, redact_paths, redact_network, redact_processes) : report;
        const std::string serialized = exported_report.dump(2, ' ', true, Json::error_handler_t::strict);
        if (options.contains("--output")) write_output(options.at("--output"), serialized, overwrite);
        if (options.contains("--html-output")) {
            write_output(options.at("--html-output"), driftglass::render_html_report(exported_report), overwrite);
        }
        if (!options.contains("--output")) std::cout << serialized << "\n";
        const auto& summary = exported_report.at("summary");
        std::cerr << "report_id=" << exported_report.at("report_id")
                  << " status=" << report.at("status")
                  << " added=" << summary.at("added_count")
                  << " removed=" << summary.at("removed_count")
                  << " changed=" << summary.at("changed_count")
                  << " uncertainties=" << summary.at("uncertainty_count") << "\n";
        return 0;
    }

    throw Error("unknown command: " + command);
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const Error& error) {
        std::cerr << "driftglass-core: " << error.what() << "\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "driftglass-core: unexpected error (" << error.what() << ")\n";
        return 3;
    }
}
