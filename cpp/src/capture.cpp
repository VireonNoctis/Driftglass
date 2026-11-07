#include "driftglass/core.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <regex>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <openssl/rand.h>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>
extern char** environ;
#endif

namespace driftglass {
namespace {

constexpr std::uint32_t kMaximumDuration = 3600;
constexpr std::uint32_t kMinimumInterval = 100;
constexpr std::uint32_t kMaximumInterval = 10000;
constexpr std::size_t kMaximumCaptureEvents = 250000;

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return result.str();
}

std::string make_run_id() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw Error("could not generate a random run identifier");
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "run-";
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) result.push_back('-');
        result.push_back(hex[bytes[index] >> 4U]);
        result.push_back(hex[bytes[index] & 0x0fU]);
    }
    return result;
}

std::filesystem::path new_output_path(const std::filesystem::path& value, const std::string& label) {
    std::error_code error;
    const auto path = std::filesystem::absolute(value, error);
    if (error) throw Error("could not resolve " + label + " path");
    const auto parent = path.parent_path();
    if (!std::filesystem::is_directory(parent)) throw Error(label + " parent directory does not exist");
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::exists(status)) throw Error(label + " already exists; capture outputs are never overwritten");
    if (error && error != std::errc::no_such_file_or_directory) throw Error("could not inspect " + label + " path");
    return path;
}

void publish_new_file(const std::filesystem::path& destination, const std::string& content) {
#if defined(__APPLE__) || defined(__linux__)
    std::string temporary = (destination.parent_path() / ".driftglass-manifest-XXXXXX").string();
    std::vector<char> path(temporary.begin(), temporary.end());
    path.push_back('\0');
    const int descriptor = ::mkstemp(path.data());
    if (descriptor < 0) throw Error("could not create temporary run manifest");
    const std::string temp_path(path.data());
    bool published = false;
    try {
        if (::fchmod(descriptor, S_IRUSR | S_IWUSR) != 0) throw Error("could not set run manifest file permissions");
        std::size_t offset = 0;
        while (offset < content.size()) {
            const auto written = ::write(descriptor, content.data() + offset, content.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw Error("could not write run manifest");
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(descriptor) != 0) throw Error("could not sync run manifest");
        if (::close(descriptor) != 0) throw Error("could not close run manifest");
        if (::link(temp_path.c_str(), destination.c_str()) != 0) {
            if (errno == EEXIST) throw Error("manifest output already exists; it was not replaced");
            throw Error("could not publish run manifest");
        }
        published = true;
        if (::unlink(temp_path.c_str()) != 0) throw Error("manifest was published but temporary-file cleanup failed");
    } catch (...) {
        if (!published) {
            ::close(descriptor);
            ::unlink(temp_path.c_str());
        }
        throw;
    }
#else
    (void)destination;
    (void)content;
    throw Error("capture manifest publication is not implemented on this platform");
#endif
}

bool executable_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

struct ObserverResult {
    std::optional<int> return_code;
    bool timed_out{false};
    std::string launch_error;
};

ObserverResult run_observer(const CaptureOptions& options, const std::filesystem::path& event_path) {
#if defined(__APPLE__) || defined(__linux__)
    std::vector<std::string> arguments{
        options.observer_path.string(),
        "--pid", std::to_string(options.pid),
        "--duration-seconds", std::to_string(options.duration_seconds),
        "--interval-ms", std::to_string(options.interval_ms),
        "--output", event_path.string()
    };
    std::vector<char*> raw_arguments;
    raw_arguments.reserve(arguments.size() + 1U);
    for (auto& argument : arguments) raw_arguments.push_back(argument.data());
    raw_arguments.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return {std::nullopt, false, "could not prepare observer process actions"};
    }
    const int spawn_error = posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (spawn_error != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return {std::nullopt, false, "could not disable observer standard input"};
    }

    pid_t child{};
    const int launched = posix_spawn(&child, options.observer_path.c_str(), &actions, nullptr,
                                     raw_arguments.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (launched != 0) {
        return {std::nullopt, false, std::string("observer could not be started (") + std::strerror(launched) + ")"};
    }

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(options.duration_seconds + 15U);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
            if (WIFEXITED(status)) return {WEXITSTATUS(status), false, {}};
            if (WIFSIGNALED(status)) return {128 + WTERMSIG(status), false, {}};
            return {std::nullopt, false, "observer exited in an unknown state"};
        }
        if (waited < 0 && errno != EINTR) {
            return {std::nullopt, false, std::string("could not wait for observer (") + std::strerror(errno) + ")"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ::kill(child, SIGTERM);
    const auto terminate_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < terminate_deadline) {
        const pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) return {std::nullopt, true, {}};
        if (waited < 0 && errno != EINTR) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ::kill(child, SIGKILL);
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return {std::nullopt, true, {}};
#else
    (void)options;
    (void)event_path;
    return {std::nullopt, false, "capture process spawning is not implemented on this platform"};
#endif
}

std::string capture_boundary_error(const std::vector<Json>& events, std::uint32_t pid) {
    std::vector<std::size_t> starts;
    std::vector<std::size_t> finishes;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& event = events[index];
        if (event.at("category") != "coverage") continue;
        if (event.at("action") == "capture_started") starts.push_back(index);
        if (event.at("action") == "capture_finished") finishes.push_back(index);
    }
    if (starts.size() != 1U) return "observer output must contain exactly one capture_started event";
    if (finishes.size() != 1U) return "observer output must contain exactly one capture_finished event";
    if (starts.front() != 0U) return "capture_started must be the first event";
    if (finishes.front() != events.size() - 1U) return "capture_finished must be the final event";

    const auto& start_attributes = events.front().at("attributes");
    if (!start_attributes.is_object() || !start_attributes.contains("target_pid")
        || !(start_attributes.at("target_pid").is_number_integer() || start_attributes.at("target_pid").is_number_unsigned())
        || start_attributes.at("target_pid") != pid) {
        return "capture_started target_pid must match the requested PID";
    }
    const auto& finish_attributes = events.back().at("attributes");
    if (!finish_attributes.is_object() || !finish_attributes.contains("capture_status")
        || !finish_attributes.at("capture_status").is_string()
        || finish_attributes.at("capture_status") != "completed") {
        return "capture_finished capture_status must be 'completed'";
    }
    return {};
}

std::vector<std::string> collect_errors(const std::vector<Json>& events, const ObserverResult& observer,
                                        const std::string& stream_error, bool started, bool finished) {
    std::vector<std::string> errors;
    if (!observer.launch_error.empty()) errors.push_back("observer could not be started");
    else if (observer.timed_out) errors.push_back("observer exceeded the coordinator time limit");
    else if (observer.return_code && *observer.return_code != 0) {
        errors.push_back("observer exited with status " + std::to_string(*observer.return_code));
    } else if (!observer.return_code) errors.push_back("observer exit status was unavailable");
    if (!stream_error.empty()) errors.push_back(stream_error);
    if (!started) errors.push_back("event stream did not contain capture_started");
    if (!finished) errors.push_back("event stream did not contain capture_finished");

    for (const auto& event : events) {
        if (event.at("category") != "coverage") continue;
        const auto action = event.at("action").get<std::string>();
        const auto& attributes = event.at("attributes");
        if (action == "coverage_gap") {
            const auto category = attributes.is_object() && attributes.contains("category")
                && attributes.at("category").is_string() ? attributes.at("category").get<std::string>() : "";
            errors.push_back(category.empty() ? "observer reported a coverage gap"
                                              : "observer reported a coverage gap for " + category);
        }
        if (action == "capture_incomplete") errors.push_back("observer reported an incomplete capture");
        if (event.at("collection_quality") != "complete") {
            errors.push_back("observer marked one or more coverage records as partial-quality");
        }
    }
    std::vector<std::string> unique;
    for (const auto& error : errors) {
        if (std::find(unique.begin(), unique.end(), error) == unique.end()) unique.push_back(error);
    }
    return unique;
}

} // namespace

CaptureResult capture_run(const CaptureOptions& options) {
    if (options.pid == 0) throw Error("pid must be a positive integer");
    if (options.duration_seconds < 1 || options.duration_seconds > kMaximumDuration) {
        throw Error("duration_seconds must be between 1 and 3600");
    }
    if (options.interval_ms < kMinimumInterval || options.interval_ms > kMaximumInterval) {
        throw Error("interval_ms must be between 100 and 10000");
    }
    if (options.role != "baseline" && options.role != "candidate") throw Error("role must be baseline or candidate");
    auto required_text = [](const std::string& value, const std::string& name, std::size_t maximum) {
        if (value.empty() || value.size() > maximum || value.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw Error(name + " must be non-empty and within its length limit");
        }
    };
    required_text(options.experiment_id, "experiment_id", 128);
    required_text(options.application_name, "application_name", 256);
    required_text(options.application_version, "application_version", 128);
    required_text(options.scenario, "scenario", 1024);
    required_text(options.profile, "profile", 128);
    if (options.package_id) required_text(*options.package_id, "package_id", 512);
    if (options.snapshot_id) required_text(*options.snapshot_id, "snapshot_id", 256);
    if (options.artifact_sha256 && !std::regex_match(*options.artifact_sha256, std::regex("^[a-f0-9]{64}$"))) {
        throw Error("artifact_sha256 must be a lowercase 64-character SHA-256 digest");
    }

    std::error_code error;
    const auto observer = std::filesystem::canonical(options.observer_path, error);
    if (error || !executable_file(observer)) throw Error("observer_path must resolve to an executable file");
    const auto event_path = new_output_path(options.event_output_path, "event output");
    const auto manifest_path = new_output_path(options.manifest_output_path, "manifest output");
    if (event_path == manifest_path) throw Error("event output and manifest output must be different paths");
    if (observer == event_path || observer == manifest_path) throw Error("capture outputs must not replace the observer executable");

    const auto started_at = timestamp_now();
    const auto observer_result = run_observer(options, event_path);

    std::vector<Json> records;
    std::string stream_error;
    try {
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(event_path, status_error);
        if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            stream_error = "observer did not create a regular event stream";
        } else {
            records = load_jsonl(event_path, options.event_schema_path);
            stream_error = capture_boundary_error(records, options.pid);
        }
    } catch (const Error&) {
        stream_error = "observer output did not pass JSONL validation";
    }

    const bool started = !records.empty() && records.front().at("category") == "coverage"
        && records.front().at("action") == "capture_started";
    const bool finished = !records.empty() && records.back().at("category") == "coverage"
        && records.back().at("action") == "capture_finished";
    const auto errors = collect_errors(records, observer_result, stream_error, started, finished);
    const bool observer_incomplete = std::any_of(records.begin(), records.end(), [](const Json& event) {
        return event.at("category") == "coverage" && event.at("action") == "capture_incomplete";
    });
    const bool completed = observer_result.return_code && *observer_result.return_code == 0
        && !observer_result.timed_out && observer_result.launch_error.empty()
        && stream_error.empty() && started && finished && !observer_incomplete;
#if defined(__APPLE__)
    const std::string platform_name = "macOS";
    const std::string source_name = "macos-libproc-polling";
    const std::vector<std::string> observer_categories{"process"};
    const std::vector<std::string> unavailable_active{"network", "file", "system", "persistence"};
#elif defined(__linux__)
    const std::string platform_name = "Linux";
    const std::string source_name = "linux-procfs-polling";
    const std::vector<std::string> observer_categories{"process", "network"};
    const std::vector<std::string> unavailable_active{"file", "system", "persistence"};
#else
    const std::string platform_name = "Other";
    const std::string source_name = "native-polling";
    const std::vector<std::string> observer_categories{"process"};
    const std::vector<std::string> unavailable_active{"network", "file", "system", "persistence"};
#endif
    struct utsname system_info {};
    std::string os_version = "unknown";
    std::string architecture = "unknown";
    if (::uname(&system_info) == 0) {
        os_version = system_info.release;
        architecture = system_info.machine;
    }
    const std::string source_state = started ? "active" : "failed";
    std::vector<std::string> unavailable = unavailable_active;
    if (!started) unavailable = {"process", "network", "file", "system", "persistence"};

    Json source_categories = observer_categories;
    Json manifest{
        {"schema_version", 1},
        {"run_id", make_run_id()},
        {"experiment_id", options.experiment_id},
        {"role", options.role},
        {"application", {
            {"name", options.application_name},
            {"version", options.application_version},
            {"artifact_sha256", options.artifact_sha256 ? Json(*options.artifact_sha256) : Json(nullptr)},
            {"package_id", options.package_id ? Json(*options.package_id) : Json(nullptr)}
        }},
        {"environment", {
            {"os", platform_name},
            {"os_version", os_version},
            {"architecture", architecture},
            {"snapshot_id", options.snapshot_id ? Json(*options.snapshot_id) : Json(nullptr)}
        }},
        {"capture", {
            {"status", completed ? "completed" : (records.empty() ? "failed" : "incomplete")},
            {"started_at", started_at},
            {"ended_at", timestamp_now()},
            {"scenario", options.scenario},
            {"profile", options.profile}
        }},
        {"coverage", {
            {"event_sources", Json::array({
                Json{{"name", source_name}, {"state", source_state}, {"categories", source_categories}}
            })},
            {"unavailable_categories", unavailable},
            {"dropped_events", 0},
            {"errors", errors}
        }}
    };
    if (!completed) manifest["capture"]["status_reason"] = records.empty()
        ? "The observer did not produce a valid capture stream."
        : "The observer did not complete the capture contract; review coverage errors and event records.";

    validate_json_schema_file(manifest, options.manifest_schema_path);
    validate_manifest(manifest);
    publish_new_file(manifest_path, manifest.dump(2, ' ', true, Json::error_handler_t::strict) + "\n");
    return CaptureResult{manifest, records.size(), event_path, manifest_path, completed};
}

} // namespace driftglass
