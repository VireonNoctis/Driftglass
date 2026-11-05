#include "driftglass/core.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
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
        << "  driftglass-core compare --baseline-manifest FILE --candidate-manifest FILE\n"
        << "      --baseline-events FILE --candidate-events FILE [--profile FILE]\n"
        << "      [--output FILE] [--overwrite]\n\n"
        << "The compare command reads existing captures. It never launches the application.\n";
}

std::map<std::string, std::string> parse_options(int argc, char** argv, int start) {
    const std::set<std::string> value_options{
        "--baseline-manifest", "--candidate-manifest",
        "--baseline-events", "--candidate-events",
        "--profile", "--output"
    };
    const std::set<std::string> flags{"--overwrite"};
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

int run(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
        print_usage();
        return argc < 2 ? 2 : 0;
    }

    const std::string command = argv[1];
    if (command == "events") {
        if (argc != 4 || std::string(argv[2]) != "validate") {
            throw Error("usage: driftglass-core events validate <events.jsonl>");
        }
        const auto events = driftglass::load_jsonl(argv[3]);
        std::cout << "valid event stream: " << events.size() << " record(s)\n";
        return 0;
    }
    if (command == "manifest") {
        if (argc != 4 || std::string(argv[2]) != "validate") {
            throw Error("usage: driftglass-core manifest validate <manifest.json>");
        }
        const auto manifest = driftglass::load_json_file(argv[3], 4U * 1024U * 1024U);
        driftglass::validate_manifest(manifest);
        std::cout << "valid run manifest: run_id=" << manifest.at("run_id")
                  << " role=" << manifest.at("role")
                  << " capture=" << manifest.at("capture").at("status")
                  << " dropped_events=" << manifest.at("coverage").at("dropped_events") << "\n";
        return 0;
    }
    if (command == "compare") {
        const auto options = parse_options(argc, argv, 2);
        const auto baseline_manifest = need(options, "--baseline-manifest");
        const auto candidate_manifest = need(options, "--candidate-manifest");
        const auto baseline_events = need(options, "--baseline-events");
        const auto candidate_events = need(options, "--candidate-events");
        const auto profile = options.contains("--profile") ? options.at("--profile") : std::string{};
        const bool overwrite = options.contains("--overwrite");

        if (overwrite && !options.contains("--output")) throw Error("--overwrite requires --output");
        if (options.contains("--output")) {
            const auto output = normalized_path(options.at("--output"));
            for (const auto& input : {baseline_manifest, candidate_manifest, baseline_events, candidate_events}) {
                if (output == normalized_path(input)) throw Error("report output must not replace an input file");
            }
            if (!profile.empty() && output == normalized_path(profile)) throw Error("report output must not replace the rule profile");
        }

        const Json report = driftglass::compare_runs(
            baseline_manifest, candidate_manifest, baseline_events, candidate_events, profile);
        const std::string serialized = report.dump(2, ' ', true, Json::error_handler_t::strict);
        if (options.contains("--output")) {
            write_output(options.at("--output"), serialized, overwrite);
        } else {
            std::cout << serialized << "\n";
        }
        const auto& summary = report.at("summary");
        std::cerr << "report_id=" << report.at("report_id")
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
